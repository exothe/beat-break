#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

/**
    A Gross-Beat style mapping curve.

    Points live in a normalised unit square: x is the position inside the
    pattern loop (0 = loop start, 1 = loop end) and y is the mapped value
    (meaning depends on the envelope: buffer position for the time curve,
    amplitude for the volume curve).

    Every point owns the shape of the segment that starts at it:
      - Curve : tension-warped ramp to the next point (tension 0 == linear)
      - Step  : hold this point's value until the next point
      - Smooth: symmetric S-curve, tension biases where the steep part sits
*/
class EnvelopeCurve
{
public:
    enum class Shape { curve = 0, step = 1, smooth = 2 };

    struct Point
    {
        float x = 0.0f;        // 0 .. 1
        float y = 0.0f;        // 0 .. 1
        float tension = 0.0f;  // -1 .. 1
        Shape shape = Shape::curve;
    };

    EnvelopeCurve() { setToRamp(); }

    //==============================================================================
    // Content

    /** Diagonal 0,0 -> 1,1: unity playback for the time curve. */
    void setToRamp()
    {
        points.clear();
        points.push_back ({ 0.0f, 0.0f, 0.0f, Shape::curve });
        points.push_back ({ 1.0f, 1.0f, 0.0f, Shape::curve });
    }

    /** Flat line at the given level: unity gain for the volume curve. */
    void setToFlat (float level)
    {
        points.clear();
        points.push_back ({ 0.0f, level, 0.0f, Shape::curve });
        points.push_back ({ 1.0f, level, 0.0f, Shape::curve });
    }

    void setPoints (std::vector<Point> newPoints)
    {
        points = std::move (newPoints);
        if (points.size() < 2)
            setToRamp();
        else
            sortAndClamp();
    }

    const std::vector<Point>& getPoints() const noexcept { return points; }
    int size() const noexcept { return (int) points.size(); }

    //==============================================================================
    // Evaluation. Realtime safe: no allocation, no locking.

    float getValue (float x) const noexcept
    {
        const auto n = (int) points.size();
        if (n == 0)
            return 0.0f;

        x = juce::jlimit (0.0f, 1.0f, x);

        if (x <= points.front().x)
            return points.front().y;
        if (x >= points.back().x)
            return points.back().y;

        // Points are kept sorted, so a binary search is fine.
        int lo = 0, hi = n - 1;
        while (hi - lo > 1)
        {
            const auto mid = (lo + hi) / 2;
            if (points[(size_t) mid].x <= x) lo = mid; else hi = mid;
        }

        const auto& a = points[(size_t) lo];
        const auto& b = points[(size_t) hi];

        if (a.shape == Shape::step)
            return a.y;

        const auto span = b.x - a.x;
        if (span <= 1.0e-9f)
            return b.y;

        const auto u = (x - a.x) / span;
        return a.y + (b.y - a.y) * shape (u, a.tension, a.shape);
    }

    /** Warps a 0..1 ramp according to tension / shape. */
    static float shape (float u, float tension, Shape s) noexcept
    {
        u = juce::jlimit (0.0f, 1.0f, u);

        if (s == Shape::step)
            return 0.0f;

        if (s == Shape::smooth)
        {
            const auto sm = u * u * (3.0f - 2.0f * u);              // smoothstep
            const auto warped = std::pow (u, std::pow (2.0f, -tension * 3.0f));
            return juce::jlimit (0.0f, 1.0f, sm * (1.0f - std::abs (tension))
                                             + warped * std::abs (tension));
        }

        if (std::abs (tension) < 1.0e-4f)
            return u;

        return std::pow (u, std::pow (2.0f, -tension * 3.0f));
    }

    //==============================================================================
    // Editing helpers (message thread only)

    int addPoint (float x, float y)
    {
        Point p { juce::jlimit (0.0f, 1.0f, x), juce::jlimit (0.0f, 1.0f, y), 0.0f, Shape::curve };

        // Inherit the shape of the segment we are splitting.
        for (size_t i = 0; i + 1 < points.size(); ++i)
            if (points[i].x <= p.x && p.x <= points[i + 1].x)
            {
                p.shape = points[i].shape;
                break;
            }

        const auto where = std::upper_bound (points.begin(), points.end(), p,
                                             [] (const Point& l, const Point& r) { return l.x < r.x; });
        const auto index = (int) std::distance (points.begin(), where);
        points.insert (where, p);
        clampAll();

        return juce::jlimit (0, (int) points.size() - 1, index);
    }

    void removePoint (int index)
    {
        if (points.size() <= 2 || ! juce::isPositiveAndBelow (index, (int) points.size()))
            return;

        points.erase (points.begin() + index);
        sortAndClamp();
    }

    /** Moves a point; end points stay pinned to x = 0 / x = 1. */
    int movePoint (int index, float x, float y)
    {
        if (! juce::isPositiveAndBelow (index, (int) points.size()))
            return index;

        auto moved = points[(size_t) index];
        const auto isFirst = (index == 0);
        const auto isLast  = (index == (int) points.size() - 1);

        moved.y = juce::jlimit (0.0f, 1.0f, y);
        if (! isFirst && ! isLast)
            moved.x = juce::jlimit (0.0f, 1.0f, x);

        points[(size_t) index] = moved;

        // Shuffle the moved point back into x order, tracking where it lands
        // so the caller can keep dragging it.
        auto i = index;
        while (i > 0 && points[(size_t) (i - 1)].x > points[(size_t) i].x)
        {
            std::swap (points[(size_t) (i - 1)], points[(size_t) i]);
            --i;
        }
        while (i + 1 < (int) points.size() && points[(size_t) (i + 1)].x < points[(size_t) i].x)
        {
            std::swap (points[(size_t) (i + 1)], points[(size_t) i]);
            ++i;
        }

        return i;
    }

    void setTension (int index, float tension)
    {
        if (juce::isPositiveAndBelow (index, (int) points.size()))
            points[(size_t) index].tension = juce::jlimit (-1.0f, 1.0f, tension);
    }

    void setShape (int index, Shape s)
    {
        if (juce::isPositiveAndBelow (index, (int) points.size()))
            points[(size_t) index].shape = s;
    }

    void setAllShapes (Shape s)
    {
        for (auto& p : points)
            p.shape = s;
    }

    void reverse()
    {
        std::vector<Point> flipped;
        flipped.reserve (points.size());

        for (auto it = points.rbegin(); it != points.rend(); ++it)
            flipped.push_back ({ 1.0f - it->x, it->y, -it->tension, it->shape });

        // Segment shapes belong to the segment start, so shift them along.
        for (size_t i = 0; i + 1 < flipped.size(); ++i)
        {
            flipped[i].tension = flipped[i + 1].tension;
            flipped[i].shape   = flipped[i + 1].shape;
        }

        setPoints (std::move (flipped));
    }

    //==============================================================================
    // Serialisation: "x,y,tension,shape;..." keeps state small and diffable.

    juce::String toString() const
    {
        juce::String s;
        for (const auto& p : points)
            s << juce::String (p.x, 6) << ',' << juce::String (p.y, 6) << ','
              << juce::String (p.tension, 4) << ',' << (int) p.shape << ';';

        return s;
    }

    static EnvelopeCurve fromString (const juce::String& text)
    {
        EnvelopeCurve c;
        std::vector<Point> parsed;

        for (const auto& chunk : juce::StringArray::fromTokens (text, ";", ""))
        {
            if (chunk.isEmpty())
                continue;

            const auto f = juce::StringArray::fromTokens (chunk, ",", "");
            if (f.size() < 2)
                continue;

            Point p;
            p.x = f[0].getFloatValue();
            p.y = f[1].getFloatValue();
            p.tension = f.size() > 2 ? f[2].getFloatValue() : 0.0f;
            p.shape = f.size() > 3 ? (Shape) juce::jlimit (0, 2, f[3].getIntValue())
                                   : Shape::curve;
            parsed.push_back (p);
        }

        if (parsed.size() >= 2)
            c.setPoints (std::move (parsed));

        return c;
    }

private:
    void clampAll()
    {
        for (auto& p : points)
        {
            p.x = juce::jlimit (0.0f, 1.0f, p.x);
            p.y = juce::jlimit (0.0f, 1.0f, p.y);
            p.tension = juce::jlimit (-1.0f, 1.0f, p.tension);
        }

        points.front().x = 0.0f;
        points.back().x  = 1.0f;
    }

    void sortAndClamp()
    {
        std::stable_sort (points.begin(), points.end(),
                          [] (const Point& l, const Point& r) { return l.x < r.x; });
        clampAll();
    }

    std::vector<Point> points;
};
