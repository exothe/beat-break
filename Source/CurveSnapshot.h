#pragma once

#include "EnvelopeCurve.h"

/**
    A fixed-size, trivially copyable copy of an EnvelopeCurve for the audio
    thread. The editor mutates EnvelopeCurve objects on the message thread and
    publishes snapshots; the audio thread only ever reads one.
*/
struct CurveSnapshot
{
    static constexpr int maxPoints = 128;

    int numPoints = 2;
    EnvelopeCurve::Point points[maxPoints] {};

    CurveSnapshot()
    {
        points[0] = { 0.0f, 0.0f, 0.0f, EnvelopeCurve::Shape::curve };
        points[1] = { 1.0f, 1.0f, 0.0f, EnvelopeCurve::Shape::curve };
    }

    void copyFrom (const EnvelopeCurve& curve) noexcept
    {
        const auto& src = curve.getPoints();
        numPoints = juce::jlimit (2, maxPoints, (int) src.size());

        for (int i = 0; i < numPoints; ++i)
            points[i] = src[(size_t) i];

        points[0].x = 0.0f;
        points[numPoints - 1].x = 1.0f;
    }

    float getValue (float x) const noexcept
    {
        x = juce::jlimit (0.0f, 1.0f, x);

        if (x <= points[0].x)
            return points[0].y;
        if (x >= points[numPoints - 1].x)
            return points[numPoints - 1].y;

        int lo = 0, hi = numPoints - 1;
        while (hi - lo > 1)
        {
            const auto mid = (lo + hi) / 2;
            if (points[mid].x <= x) lo = mid; else hi = mid;
        }

        const auto& a = points[lo];
        const auto& b = points[hi];

        if (a.shape == EnvelopeCurve::Shape::step)
            return a.y;

        const auto span = b.x - a.x;
        if (span <= 1.0e-9f)
            return b.y;

        return a.y + (b.y - a.y)
                   * EnvelopeCurve::shape ((x - a.x) / span, a.tension, a.shape);
    }
};

/**
    Double-buffered snapshot holder: one writer (message thread), one reader
    (audio thread), no locks.
*/
class PublishedCurve
{
public:
    void publish (const EnvelopeCurve& curve)
    {
        const auto next = 1 - live.load (std::memory_order_relaxed);
        buffers[(size_t) next].copyFrom (curve);
        live.store (next, std::memory_order_release);
    }

    const CurveSnapshot& get() const noexcept
    {
        return buffers[(size_t) live.load (std::memory_order_acquire)];
    }

private:
    CurveSnapshot buffers[2];
    std::atomic<int> live { 0 };
};
