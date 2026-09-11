#include "FactoryPatterns.h"

namespace FactoryPatterns
{

using Pt = EnvelopeCurve::Point;
using Shape = EnvelopeCurve::Shape;

namespace
{
    // Gap left before a slice boundary so the jump back is a discontinuity
    // rather than a fast ramp.
    constexpr float edge = 0.0006f;

    Pt pt (float x, float y, float tension = 0.0f, Shape shape = Shape::curve)
    {
        return { x, y, tension, shape };
    }

    EnvelopeCurve build (std::vector<Pt> pts)
    {
        EnvelopeCurve c;
        if (pts.size() >= 2)
            c.setPoints (std::move (pts));

        return c;
    }

    /** Plays buffer material from srcStart across the x window [x0, x1) at the
        given rate: 1 = unity speed, 0.5 = an octave down, -1 = backwards.

        The window stops `edge` short of x1 so the jump into the next window is
        a discontinuity rather than a fast ramp. The source length is derived
        from the shortened window, which keeps the playback rate exact - if it
        were derived from the full window, every slice would run slightly
        sharp. */
    void appendPlay (std::vector<Pt>& pts, float x0, float x1, float srcStart, float rate,
                     bool lastWindow)
    {
        const auto xEnd = lastWindow ? x1 : juce::jmax (x0 + 1.0e-4f, x1 - edge);
        const auto srcLength = (xEnd - x0) * rate;

        pts.push_back (pt (x0, juce::jlimit (0.0f, 1.0f, srcStart)));
        pts.push_back (pt (xEnd, juce::jlimit (0.0f, 1.0f, srcStart + srcLength)));
    }

    /** n equal slices; sourceOf(i) returns where slice i reads from and at
        what rate it plays. */
    EnvelopeCurve slicedTime (int n, const std::function<std::pair<float, float> (int)>& sourceOf)
    {
        std::vector<Pt> pts;
        pts.reserve ((size_t) n * 2);

        for (int i = 0; i < n; ++i)
        {
            const auto source = sourceOf (i);
            appendPlay (pts, (float) i / (float) n, (float) (i + 1) / (float) n,
                        source.first, source.second, i == n - 1);
        }

        return build (std::move (pts));
    }

    /** Every slice replays the same source slice: the classic stutter. */
    EnvelopeCurve stutter (int n, int sourceSlice = 0)
    {
        const auto len = 1.0f / (float) n;
        return slicedTime (n, [sourceSlice, len] (int) -> std::pair<float, float>
        {
            return { (float) sourceSlice * len, 1.0f };
        });
    }

    /** Every slice replays the slice before it: a one-slice roll. */
    EnvelopeCurve roll (int n, bool reversed = false)
    {
        const auto len = 1.0f / (float) n;
        return slicedTime (n, [len, reversed] (int i) -> std::pair<float, float>
        {
            const auto src = juce::jmax (0.0f, (float) i * len - len);
            return reversed ? std::pair<float, float> { src + len, -1.0f }
                            : std::pair<float, float> { src, 1.0f };
        });
    }

    /** Slices that read themselves at the given speed (0.5 = octave down). */
    EnvelopeCurve slowSlices (int n, float speed)
    {
        const auto len = 1.0f / (float) n;
        return slicedTime (n, [len, speed] (int i) -> std::pair<float, float>
        {
            return { (float) i * len, speed };
        });
    }

    /** Mask-driven mix of unity slices and repeats: bit set == repeat. */
    EnvelopeCurve maskedTime (int n, juce::uint32 mask)
    {
        const auto len = 1.0f / (float) n;
        return slicedTime (n, [len, mask] (int i) -> std::pair<float, float>
        {
            const auto repeat = (mask >> (i % 32)) & 1u;
            const auto src = repeat != 0 ? juce::jmax (0.0f, (float) i * len - len)
                                         : (float) i * len;
            return { src, 1.0f };
        });
    }

    //==========================================================================
    // Volume helpers

    /** Hard gate: n slices, each open for `duty` of its length. */
    EnvelopeCurve gate (int n, float duty, juce::uint32 mask = 0xffffffffu,
                        float low = 0.0f, float high = 1.0f)
    {
        std::vector<Pt> pts;

        for (int i = 0; i < n; ++i)
        {
            const auto x0 = (float) i / (float) n;
            const auto open = ((mask >> (i % 32)) & 1u) != 0u;
            const auto level = open ? high : low;

            pts.push_back (pt (x0, level, 0.0f, Shape::step));
            pts.push_back (pt (juce::jmin (1.0f, x0 + duty / (float) n), low, 0.0f, Shape::step));
        }

        pts.push_back (pt (1.0f, low, 0.0f, Shape::step));
        return build (std::move (pts));
    }

    /** Soft gate with ramped edges: `duty` open, `slew` of a slice per edge. */
    EnvelopeCurve softGate (int n, float duty, float slew, float low = 0.0f)
    {
        std::vector<Pt> pts;
        const auto w = 1.0f / (float) n;

        for (int i = 0; i < n; ++i)
        {
            const auto x0 = (float) i / (float) n;
            pts.push_back (pt (x0, low));
            pts.push_back (pt (x0 + slew * w, 1.0f));
            pts.push_back (pt (x0 + juce::jmax (slew, duty) * w, 1.0f));
            pts.push_back (pt (juce::jmin (1.0f, x0 + (juce::jmax (slew, duty) + slew) * w), low));
        }

        pts.push_back (pt (1.0f, low));
        return build (std::move (pts));
    }

    /** Sidechain style duck: instant drop on the beat, curved recovery. */
    EnvelopeCurve duck (int n, float depth, float recovery, float tension = 0.6f)
    {
        std::vector<Pt> pts;
        const auto w = 1.0f / (float) n;

        for (int i = 0; i < n; ++i)
        {
            const auto x0 = (float) i / (float) n;
            pts.push_back (pt (x0, depth, tension));
            pts.push_back (pt (juce::jmin (1.0f, x0 + recovery * w), 1.0f));

            if (i < n - 1)
                pts.push_back (pt ((float) (i + 1) / (float) n - edge, 1.0f));
        }

        pts.push_back (pt (1.0f, 1.0f));
        return build (std::move (pts));
    }

    /** Smooth tremolo: n dips using S-curves. */
    EnvelopeCurve tremolo (int n, float depth)
    {
        std::vector<Pt> pts;
        const auto w = 1.0f / (float) n;

        for (int i = 0; i < n; ++i)
        {
            const auto x0 = (float) i / (float) n;
            pts.push_back (pt (x0, 1.0f, 0.0f, Shape::smooth));
            pts.push_back (pt (x0 + 0.5f * w, depth, 0.0f, Shape::smooth));
        }

        pts.push_back (pt (1.0f, 1.0f, 0.0f, Shape::smooth));
        return build (std::move (pts));
    }

    /** Ramp up inside every slice: backwards-sounding gate. */
    EnvelopeCurve reverseGate (int n, float tension)
    {
        std::vector<Pt> pts;

        for (int i = 0; i < n; ++i)
        {
            const auto x0 = (float) i / (float) n;
            pts.push_back (pt (x0, 0.0f, tension));

            if (i < n - 1)
                pts.push_back (pt ((float) (i + 1) / (float) n - edge, 1.0f));
        }

        pts.push_back (pt (1.0f, 1.0f));
        return build (std::move (pts));
    }
}

//==============================================================================

juce::String getTimeName (int slot)
{
    static const char* names[numSlots] =
    {
        "Off",              "Stutter 1/4",      "Stutter 1/8",      "Stutter 1/16",
        "Stutter 1/32",     "Roll 1/8",         "Roll 1/16",        "Roll Build",
        "1/2 Speed",        "1/2 Speed x2",     "1/4 Speed",        "Octave Down 1/4",
        "2x Second Half",   "Reverse 1/4",      "Reverse 1/8",      "Reverse Half",
        "Tape Stop",        "Tape Start",       "Scratch 1",        "Scratch 2",
        "Triplet 1/12",     "Sextuplet 1/24",   "Skip 1/8",         "Ping Pong 1/8",
        "Freeze 1/32",      "Rewind",           "Glitch A",         "Glitch B",
        "Chop 1/6",         "Roll Accelerate",  "Hold Last 1/16",   "Half Bar Echo",
        "Echo 1/4",         "Slow Down",        "Speed Up",         "Stutter Half"
    };

    return juce::isPositiveAndBelow (slot, numSlots) ? juce::String (names[slot])
                                                     : juce::String ("Slot ") + juce::String (slot + 1);
}

juce::String getVolumeName (int slot)
{
    static const char* names[numSlots] =
    {
        "Off",              "Gate 1/4",         "Gate 1/8",         "Gate 1/16",
        "Gate 1/32",        "Offbeat 1/8",      "Trance 1/16",      "Sidechain 1/4",
        "Sidechain 1/8",    "Tremolo 1/8",      "Tremolo 1/16",     "Fade In",
        "Fade Out",         "Mute 2nd Half",    "Mute 1st Half",    "Gate Build",
        "Pump 1/4",         "Half Level",       "Gate 1/6",         "Gate 1/12",
        "Soft Chop 1/8",    "Duck Last 1/8",    "Duck First 1/8",   "Short 1/4",
        "Short 1/8",        "Fade Out Tail",    "Fade In Head",     "Every Other 1/8",
        "Pattern A",        "Pattern B",        "Swell x2",         "Swell x4",
        "Reverse Gate 1/8", "Reverse Gate 1/16","Mute 3rd Beat",    "Half Then Full"
    };

    return juce::isPositiveAndBelow (slot, numSlots) ? juce::String (names[slot])
                                                     : juce::String ("Slot ") + juce::String (slot + 1);
}

//==============================================================================

EnvelopeCurve getTimeCurve (int slot)
{
    switch (slot)
    {
        case 0:  break;                                     // unity ramp
        case 1:  return stutter (4);
        case 2:  return stutter (8);
        case 3:  return stutter (16);
        case 4:  return stutter (32);
        case 5:  return roll (8);
        case 6:  return roll (16);

        case 7:                                             // rate doubles twice
        {
            std::vector<Pt> pts;
            appendPlay (pts, 0.0f,    0.25f,  0.0f,     1.0f,    false);
            appendPlay (pts, 0.25f,   0.5f,   0.0f,     1.0f,    false);
            appendPlay (pts, 0.5f,    0.625f, 0.0f,     1.0f,    false);
            appendPlay (pts, 0.625f,  0.75f,  0.0f,     1.0f,    false);
            for (int i = 0; i < 4; ++i)
                appendPlay (pts, 0.75f + (float) i * 0.0625f, 0.75f + (float) (i + 1) * 0.0625f,
                            0.0f, 1.0f, i == 3);
            return build (std::move (pts));
        }

        case 8:  return build ({ pt (0.0f, 0.0f), pt (1.0f, 0.5f) });
        case 9:  return build ({ pt (0.0f, 0.0f), pt (0.5f - edge, 0.25f),
                                 pt (0.5f, 0.5f), pt (1.0f, 0.75f) });
        case 10: return build ({ pt (0.0f, 0.0f), pt (1.0f, 0.25f) });
        case 11: return slowSlices (4, 0.5f);
        case 12: return build ({ pt (0.0f, 0.0f), pt (0.5f - edge, 0.5f),
                                 pt (0.5f, 0.0f), pt (1.0f, 1.0f) });
        case 13: return roll (4, true);
        case 14: return roll (8, true);
        case 15: return build ({ pt (0.0f, 0.0f), pt (0.5f - edge, 0.5f),
                                 pt (0.5f, 0.5f), pt (1.0f, 0.0f) });

        case 16: return build ({ pt (0.0f, 0.0f), pt (0.45f, 0.45f, 0.75f), pt (1.0f, 0.56f) });
        case 17: return build ({ pt (0.0f, 0.0f, -0.8f), pt (0.55f, 0.2f), pt (1.0f, 0.65f) });

        case 18:                                            // slow forward, fast back
            return build ({ pt (0.0f, 0.0f), pt (0.25f, 0.25f), pt (0.4f, 0.05f),
                            pt (0.6f, 0.35f), pt (0.75f, 0.1f), pt (1.0f, 0.45f) });
        case 19:
            return build ({ pt (0.0f, 0.0f), pt (0.12f, 0.12f), pt (0.2f, 0.02f),
                            pt (0.32f, 0.2f), pt (0.4f, 0.06f), pt (0.55f, 0.3f),
                            pt (0.62f, 0.12f), pt (0.78f, 0.42f), pt (0.86f, 0.2f),
                            pt (1.0f, 0.5f) });

        case 20: return stutter (12);
        case 21: return stutter (24);
        case 22: return maskedTime (8, 0b10101010u);
        case 23: return slicedTime (8, [] (int i) -> std::pair<float, float>
        {
            const auto src = juce::jmax (0.0f, (float) i * 0.125f - 0.125f);
            return (i % 2) == 0 ? std::pair<float, float> { src, 1.0f }
                                : std::pair<float, float> { src + 0.125f, -1.0f };
        });
        case 24: return stutter (32);
        case 25: return build ({ pt (0.0f, 0.0f), pt (0.5f, 0.5f), pt (1.0f, 0.0f) });

        case 26: return slicedTime (8, [] (int i) -> std::pair<float, float>
        {
            static const float src[8] = { 0.0f, 0.125f, 0.0f, 0.25f, 0.125f, 0.5f, 0.25f, 0.625f };
            return { juce::jmin (src[i], (float) i * 0.125f), 1.0f };
        });
        case 27: return slicedTime (16, [] (int i) -> std::pair<float, float>
        {
            static const float src[16] = { 0.0f, 0.0f, 0.125f, 0.0625f, 0.25f, 0.1875f, 0.125f, 0.375f,
                                           0.5f, 0.4375f, 0.25f, 0.625f, 0.375f, 0.75f, 0.5f, 0.8125f };
            const auto s = juce::jmin (src[i], (float) i * 0.0625f);
            return (i % 4) == 3 ? std::pair<float, float> { s + 0.0625f, -1.0f }
                                : std::pair<float, float> { s, 1.0f };
        });

        case 28: return roll (6);
        case 29:                                            // 1/8, 1/8, 1/16 x2, 1/32 x4
        {
            std::vector<Pt> pts;
            appendPlay (pts, 0.0f,   0.25f,  0.0f, 1.0f, false);
            appendPlay (pts, 0.25f,  0.375f, 0.0f, 1.0f, false);
            appendPlay (pts, 0.375f, 0.5f,   0.0f, 1.0f, false);
            for (int i = 0; i < 4; ++i)
                appendPlay (pts, 0.5f + (float) i * 0.0625f, 0.5f + (float) (i + 1) * 0.0625f,
                            0.0f, 1.0f, false);
            for (int i = 0; i < 8; ++i)
                appendPlay (pts, 0.75f + (float) i * 0.03125f, 0.75f + (float) (i + 1) * 0.03125f,
                            0.0f, 1.0f, i == 7);
            return build (std::move (pts));
        }

        case 30:                                            // unity, then hold the last 1/16
        {
            std::vector<Pt> pts;
            pts.push_back (pt (0.0f, 0.0f));
            pts.push_back (pt (0.75f - edge, 0.75f));
            for (int i = 0; i < 4; ++i)
                appendPlay (pts, 0.75f + (float) i * 0.0625f, 0.75f + (float) (i + 1) * 0.0625f,
                            0.6875f, 1.0f, i == 3);
            return build (std::move (pts));
        }

        case 31: return build ({ pt (0.0f, 0.0f), pt (0.5f - edge, 0.5f),
                                 pt (0.5f, 0.0f), pt (1.0f, 0.5f) });
        case 32: return roll (4);
        case 33: return build ({ pt (0.0f, 0.0f), pt (0.3f, 0.3f, 0.35f),
                                 pt (0.7f, 0.52f, 0.6f), pt (1.0f, 0.62f) });
        case 34: return build ({ pt (0.0f, 0.0f, -0.6f), pt (0.4f, 0.12f),
                                 pt (0.7f, 0.42f, -0.3f), pt (1.0f, 0.9f) });
        case 35:
        {
            std::vector<Pt> pts;
            pts.push_back (pt (0.0f, 0.0f));
            pts.push_back (pt (0.5f - edge, 0.5f));
            for (int i = 0; i < 8; ++i)
                appendPlay (pts, 0.5f + (float) i * 0.0625f, 0.5f + (float) (i + 1) * 0.0625f,
                            0.4375f, 1.0f, i == 7);
            return build (std::move (pts));
        }

        default: break;
    }

    EnvelopeCurve unity;
    unity.setToRamp();
    return unity;
}

EnvelopeCurve getVolumeCurve (int slot)
{
    switch (slot)
    {
        case 0:  break;                                     // flat, unity gain
        case 1:  return gate (4, 0.6f);
        case 2:  return gate (8, 0.6f);
        case 3:  return gate (16, 0.6f);
        case 4:  return gate (32, 0.6f);
        case 5:  return gate (8, 0.6f, 0b10101010u);
        case 6:  return gate (16, 0.55f, 0b1011011010111011u);
        case 7:  return duck (4, 0.0f, 0.75f);
        case 8:  return duck (8, 0.15f, 0.7f);
        case 9:  return tremolo (8, 0.15f);
        case 10: return tremolo (16, 0.25f);
        case 11: return build ({ pt (0.0f, 0.0f, -0.3f), pt (1.0f, 1.0f) });
        case 12: return build ({ pt (0.0f, 1.0f, 0.3f), pt (1.0f, 0.0f) });
        case 13: return build ({ pt (0.0f, 1.0f, 0.0f, Shape::step), pt (0.5f, 0.0f, 0.0f, Shape::step),
                                 pt (1.0f, 0.0f, 0.0f, Shape::step) });
        case 14: return build ({ pt (0.0f, 0.0f, 0.0f, Shape::step), pt (0.5f, 1.0f, 0.0f, Shape::step),
                                 pt (1.0f, 1.0f, 0.0f, Shape::step) });

        case 15:                                            // gates getting faster
        {
            std::vector<Pt> pts;
            const auto addGate = [&pts] (float x0, float w, float duty)
            {
                pts.push_back (pt (x0, 1.0f, 0.0f, Shape::step));
                pts.push_back (pt (x0 + w * duty, 0.0f, 0.0f, Shape::step));
            };
            addGate (0.0f, 0.25f, 0.6f);
            addGate (0.25f, 0.25f, 0.6f);
            addGate (0.5f, 0.125f, 0.6f);
            addGate (0.625f, 0.125f, 0.6f);
            for (int i = 0; i < 4; ++i)
                addGate (0.75f + (float) i * 0.0625f, 0.0625f, 0.6f);
            pts.push_back (pt (1.0f, 0.0f, 0.0f, Shape::step));
            return build (std::move (pts));
        }

        case 16: return duck (4, 0.25f, 0.9f, -0.5f);
        case 17: return build ({ pt (0.0f, 0.5f), pt (1.0f, 0.5f) });
        case 18: return gate (6, 0.6f);
        case 19: return gate (12, 0.6f);
        case 20: return softGate (8, 0.55f, 0.12f);
        case 21: return build ({ pt (0.0f, 1.0f), pt (0.875f - edge, 1.0f),
                                 pt (0.875f, 0.0f, 0.0f, Shape::step), pt (1.0f, 0.0f) });
        case 22: return build ({ pt (0.0f, 0.0f, 0.0f, Shape::step), pt (0.125f, 1.0f), pt (1.0f, 1.0f) });
        case 23: return gate (4, 0.25f);
        case 24: return gate (8, 0.25f);
        case 25: return build ({ pt (0.0f, 1.0f), pt (0.75f, 1.0f, 0.4f), pt (1.0f, 0.0f) });
        case 26: return build ({ pt (0.0f, 0.0f, -0.4f), pt (0.25f, 1.0f), pt (1.0f, 1.0f) });
        case 27: return gate (8, 0.95f, 0b01010101u);
        case 28: return gate (16, 0.7f, 0b1101001110110101u);
        case 29: return gate (16, 0.5f, 0b1110010110101101u);
        case 30: return tremolo (2, 0.0f);
        case 31: return tremolo (4, 0.0f);
        case 32: return reverseGate (8, 0.5f);
        case 33: return reverseGate (16, 0.5f);
        case 34: return build ({ pt (0.0f, 1.0f, 0.0f, Shape::step), pt (0.5f, 0.0f, 0.0f, Shape::step),
                                 pt (0.75f, 1.0f, 0.0f, Shape::step), pt (1.0f, 1.0f, 0.0f, Shape::step) });
        case 35: return build ({ pt (0.0f, 0.5f, 0.0f, Shape::step), pt (0.5f, 1.0f, 0.0f, Shape::step),
                                 pt (1.0f, 1.0f, 0.0f, Shape::step) });
        default: break;
    }

    EnvelopeCurve flat;
    flat.setToFlat (1.0f);
    return flat;
}

} // namespace FactoryPatterns
