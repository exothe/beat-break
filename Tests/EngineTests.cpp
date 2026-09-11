/*
    Offline checks for the engine: render a known signal through it and verify
    the read pointer really lands where the time curve says it should.

    Build:  cmake --build build --target BeatBreakTests
    Run:    ./build/BeatBreakTests_artefacts/Release/BeatBreakTests
*/

#include "../Source/FactoryPatterns.h"
#include "../Source/GrossEngine.h"

#include <iostream>

namespace
{
    constexpr double sr = 48000.0;
    constexpr double bpm = 120.0;
    constexpr int blockSize = 512;

    int failures = 0;

    void check (bool condition, const juce::String& what)
    {
        std::cout << (condition ? "  pass  " : "  FAIL  ") << what << std::endl;
        if (! condition)
            ++failures;
    }

    /** A ramp that encodes absolute sample position, so the output value tells
        us exactly which input sample was read. */
    float positionSignal (juce::int64 sampleIndex)
    {
        return (float) ((double) sampleIndex / sr);
    }

    struct RenderResult
    {
        std::vector<float> output;      // rendered wet signal
        std::vector<juce::int64> index; // absolute sample index per output frame
    };

    RenderResult render (const EnvelopeCurve& timeCurve,
                         const EnvelopeCurve& volCurve,
                         double seconds,
                         GrossEngine::Params params)
    {
        GrossEngine engine;
        engine.prepare (sr, 1, blockSize);

        CurveSnapshot timeSnap, volSnap;
        timeSnap.copyFrom (timeCurve);
        volSnap.copyFrom (volCurve);

        const auto beatsPerSample = bpm / 60.0 / sr;
        const auto totalSamples = (juce::int64) (seconds * sr);

        RenderResult result;
        result.output.reserve ((size_t) totalSamples);
        result.index.reserve ((size_t) totalSamples);

        juce::AudioBuffer<float> buffer (1, blockSize);
        juce::int64 pos = 0;

        while (pos < totalSamples)
        {
            const auto n = (int) juce::jmin ((juce::int64) blockSize, totalSamples - pos);
            buffer.setSize (1, n, false, false, true);

            for (int i = 0; i < n; ++i)
                buffer.setSample (0, i, positionSignal (pos + i));

            engine.process (buffer, params, timeSnap, volSnap,
                            (double) pos * beatsPerSample, beatsPerSample);

            for (int i = 0; i < n; ++i)
            {
                result.output.push_back (buffer.getSample (0, i));
                result.index.push_back (pos + i);
            }

            pos += n;
        }

        return result;
    }

    GrossEngine::Params defaultParams()
    {
        GrossEngine::Params p;
        p.mix = 1.0f;
        p.timeAmount = 1.0f;
        p.volAmount = 1.0f;
        p.smoothingMs = 0.0f;
        p.loopBeats = 4.0f;
        p.spanBeats = 4.0f;
        return p;
    }

    /** Where in absolute time the output frame came from, in samples. */
    double sourceOf (const RenderResult& r, size_t frame)
    {
        jassert (frame < r.output.size());
        return (double) r.output[frame] * sr;
    }
}

int main()
{
    std::cout << "BeatBreak engine tests" << std::endl;

    const auto samplesPerBeat = sr * 60.0 / bpm;
    const auto samplesPerLoop = samplesPerBeat * 4.0;   // 1 bar at 4/4

    // ---- 1. unity curve is a pass-through --------------------------------
    {
        EnvelopeCurve unity, flat;
        unity.setToRamp();
        flat.setToFlat (1.0f);

        const auto r = render (unity, flat, 12.0, defaultParams());
        auto worst = 0.0;

        for (size_t i = (size_t) sr; i < r.output.size(); ++i)   // skip the first second
            worst = juce::jmax (worst, std::abs (sourceOf (r, i) - (double) r.index[i]));

        // The probe signal is a float32 ramp in seconds, so ~0.05 samples of
        // quantisation is expected at the end of a 12 s render.
        check (worst < 0.25, "unity time curve reads the current sample (max error "
                                 + juce::String (worst, 4) + " samples)");
    }

    // ---- 2. flat curve freezes ---------------------------------------------
    {
        EnvelopeCurve frozen, flat;
        frozen.setPoints ({ { 0.0f, 0.25f, 0.0f, EnvelopeCurve::Shape::curve },
                            { 1.0f, 0.25f, 0.0f, EnvelopeCurve::Shape::curve } });
        flat.setToFlat (1.0f);

        const auto r = render (frozen, flat, 12.0, defaultParams());

        // Inside one loop the source must stay put (same sample repeated).
        // y = 0.25 is only reachable once the playhead has passed x = 0.25;
        // before that the mapped position would be in the future and the
        // engine clamps to "now", so probe the second half of the loop.
        const auto start = (size_t) (samplesPerLoop * 4.4);
        const auto end   = (size_t) (samplesPerLoop * 4.9);
        auto spread = 0.0;
        const auto first = sourceOf (r, start);

        for (auto i = start; i < end; ++i)
            spread = juce::jmax (spread, std::abs (sourceOf (r, i) - first));

        check (spread < 2.0, "flat time curve holds one position (spread "
                                 + juce::String (spread, 2) + " samples)");
    }

    // ---- 3. half speed halves the read rate --------------------------------
    {
        EnvelopeCurve half, flat;
        half.setPoints ({ { 0.0f, 0.0f, 0.0f, EnvelopeCurve::Shape::curve },
                          { 1.0f, 0.5f, 0.0f, EnvelopeCurve::Shape::curve } });
        flat.setToFlat (1.0f);

        const auto r = render (half, flat, 12.0, defaultParams());
        const auto a = (size_t) (samplesPerLoop * 4.1);
        const auto b = (size_t) (samplesPerLoop * 4.6);
        const auto rate = (sourceOf (r, b) - sourceOf (r, a)) / (double) (b - a);

        check (std::abs (rate - 0.5) < 0.02, "1/2 speed curve advances at half rate (measured "
                                                 + juce::String (rate, 4) + ")");
    }

    // ---- 4. stutter 1/8 replays the first eighth ---------------------------
    {
        const auto stutter = FactoryPatterns::getTimeCurve (2);   // "Stutter 1/8"
        EnvelopeCurve flat;
        flat.setToFlat (1.0f);

        const auto r = render (stutter, flat, 12.0, defaultParams());
        const auto loopStart = (size_t) (samplesPerLoop * 4.0);
        const auto eighth = samplesPerLoop / 8.0;
        auto worst = 0.0;

        // Every eighth should read the same slice of the loop, so the source
        // position modulo one eighth must track the position inside the slice.
        for (int slice = 1; slice < 8; ++slice)
        {
            const auto probe = loopStart + (size_t) (eighth * ((double) slice + 0.5));
            const auto expected = (double) loopStart + eighth * 0.5;
            worst = juce::jmax (worst, std::abs (sourceOf (r, probe) - expected));
        }

        check (worst < eighth * 0.05, "stutter 1/8 re-reads the first eighth (max error "
                                          + juce::String (worst, 1) + " samples)");
    }

    // ---- 5. reverse slices run backwards -----------------------------------
    {
        const auto reverse = FactoryPatterns::getTimeCurve (14);  // "Reverse 1/8"
        EnvelopeCurve flat;
        flat.setToFlat (1.0f);

        const auto r = render (reverse, flat, 12.0, defaultParams());
        const auto loopStart = (size_t) (samplesPerLoop * 4.0);
        const auto eighth = samplesPerLoop / 8.0;
        const auto a = loopStart + (size_t) (eighth * 3.2);
        const auto b = loopStart + (size_t) (eighth * 3.8);
        const auto rate = (sourceOf (r, b) - sourceOf (r, a)) / (double) (b - a);

        check (rate < -0.9 && rate > -1.1, "reverse 1/8 reads backwards at unity speed (measured "
                                               + juce::String (rate, 3) + ")");
    }

    // ---- 6. volume curve gates ---------------------------------------------
    {
        EnvelopeCurve unity;
        unity.setToRamp();
        const auto gate = FactoryPatterns::getVolumeCurve (2);   // "Gate 1/8", 60% duty

        auto params = defaultParams();
        const auto r = render (unity, gate, 12.0, params);
        const auto loopStart = (size_t) (samplesPerLoop * 4.0);
        const auto eighth = samplesPerLoop / 8.0;

        const auto openFrame = loopStart + (size_t) (eighth * 0.3);
        const auto shutFrame = loopStart + (size_t) (eighth * 0.9);

        const auto openLevel = std::abs (r.output[openFrame]);
        const auto shutLevel = std::abs (r.output[shutFrame]);
        const auto inputLevel = std::abs (positionSignal (r.index[openFrame]));

        check (openLevel > inputLevel * 0.95, "gate open passes signal");
        check (shutLevel < inputLevel * 0.02, "gate closed mutes signal");
    }

    // ---- 7. amount = 0 bypasses each curve ---------------------------------
    {
        const auto stutter = FactoryPatterns::getTimeCurve (3);
        const auto gate = FactoryPatterns::getVolumeCurve (3);

        auto params = defaultParams();
        params.timeAmount = 0.0f;
        params.volAmount = 0.0f;

        const auto r = render (stutter, gate, 8.0, params);
        auto worst = 0.0;

        for (size_t i = (size_t) sr; i < r.output.size(); ++i)
            worst = juce::jmax (worst, std::abs (sourceOf (r, i) - (double) r.index[i]));

        check (worst < 0.25, "amount 0 leaves the signal untouched (max error "
                                 + juce::String (worst, 4) + " samples)");
    }

    // ---- 8. every factory pattern stays finite and in range ----------------
    {
        auto allFinite = true;
        auto worstPeak = 0.0f;

        for (int slot = 0; slot < FactoryPatterns::numSlots; ++slot)
        {
            const auto timeCurve = FactoryPatterns::getTimeCurve (slot);
            const auto volCurve = FactoryPatterns::getVolumeCurve (slot);

            auto params = defaultParams();
            params.smoothingMs = (float) (slot % 4) * 12.0f;

            const auto r = render (timeCurve, volCurve, 2.5, params);

            for (auto v : r.output)
            {
                if (! std::isfinite (v))
                    allFinite = false;

                worstPeak = juce::jmax (worstPeak, std::abs (v));
            }
        }

        check (allFinite, "all 36 factory slots render finite output");
        check (worstPeak < 4.0f, "no factory slot blows up the level (peak "
                                     + juce::String (worstPeak, 3) + " of a 0..2.5 ramp input)");
    }

    // ---- 9. jumps are declicked --------------------------------------------
    {
        const auto stutter = FactoryPatterns::getTimeCurve (1);   // "Stutter 1/4"
        EnvelopeCurve flat;
        flat.setToFlat (1.0f);

        // A sine makes discontinuities easy to measure as a sample delta.
        GrossEngine engine;
        engine.prepare (sr, 1, blockSize);

        CurveSnapshot timeSnap, volSnap;
        timeSnap.copyFrom (stutter);
        volSnap.copyFrom (flat);

        const auto beatsPerSample = bpm / 60.0 / sr;
        const auto totalSamples = (juce::int64) (6.0 * sr);
        auto params = defaultParams();

        juce::AudioBuffer<float> buffer (1, blockSize);
        juce::int64 pos = 0;
        auto previous = 0.0f;
        auto maxStep = 0.0f;

        while (pos < totalSamples)
        {
            const auto n = (int) juce::jmin ((juce::int64) blockSize, totalSamples - pos);
            buffer.setSize (1, n, false, false, true);

            for (int i = 0; i < n; ++i)
                buffer.setSample (0, i, std::sin (juce::MathConstants<float>::twoPi * 220.0f
                                                  * (float) ((double) (pos + i) / sr)));

            engine.process (buffer, params, timeSnap, volSnap,
                            (double) pos * beatsPerSample, beatsPerSample);

            if (pos > (juce::int64) (sr * 2))
                for (int i = 0; i < n; ++i)
                {
                    const auto s = buffer.getSample (0, i);
                    maxStep = juce::jmax (maxStep, std::abs (s - previous));
                    previous = s;
                }
            else
                previous = buffer.getSample (0, n - 1);

            pos += n;
        }

        // A 220 Hz sine at 48 kHz moves at most ~0.03 per sample; a hard
        // splice would show up as a step near 2.0.
        check (maxStep < 0.2f, "stutter jumps are crossfaded (largest sample step "
                                   + juce::String (maxStep, 4) + ")");
    }


    // ---- 10. volume attack / release / tension -----------------------------
    {
        EnvelopeCurve unity;
        unity.setToRamp();
        const auto gate = FactoryPatterns::getVolumeCurve (2);   // "Gate 1/8"

        // With a unity time curve the read position is "now", so the output
        // divided by the input ramp is exactly the envelope's gain.
        const auto gainOf = [] (const RenderResult& r, size_t frame)
        {
            const auto in = (double) positionSignal (r.index[frame]);
            return in > 1.0e-6 ? (double) r.output[frame] / in : 1.0;
        };

        // Seconds the envelope takes to travel from unity to `level` (falling)
        // or from silence to `level` (rising), on the first move after `from`.
        const auto travelTime = [&] (const RenderResult& r, size_t from, double level, bool falling)
        {
            size_t started = 0;

            for (size_t i = from; i < r.output.size(); ++i)
            {
                const auto g = gainOf (r, i);

                if (started == 0)
                {
                    if (falling ? g < 0.99 : g > 0.01)
                        started = i;

                    continue;
                }

                if (falling ? g < level : g > level)
                    return (double) (i - started) / sr;
            }

            return -1.0;
        };

        // Probe inside a settled open window (as check 6 does), so the first
        // move the scan sees is the gate closing.
        const auto eighth = samplesPerLoop / 8.0;
        const auto probe = (size_t) (samplesPerLoop * 4.0 + eighth * 0.3);

        auto slow = defaultParams();
        slow.volAttackMs = 50.0f;
        slow.volReleaseMs = 50.0f;
        const auto rSlow = render (unity, gate, 12.0, slow);

        // Full scale takes the knob's time, so 1 -> 0.02 is ~0.98 of 50 ms.
        const auto fall = travelTime (rSlow, probe, 0.02, true);
        check (fall > 0.04 && fall < 0.06, "50 ms release takes 50 ms to close (measured "
                                               + juce::String (fall * 1000.0, 1) + " ms)");

        // Scan from a settled closed window so the next move is the gate opening.
        const auto closedProbe = (size_t) (samplesPerLoop * 4.0 + eighth * 0.9);
        const auto rise = travelTime (rSlow, closedProbe, 0.98, false);
        check (rise > 0.04 && rise < 0.06, "50 ms attack takes 50 ms to open (measured "
                                               + juce::String (rise * 1000.0, 1) + " ms)");

        auto fast = defaultParams();
        fast.volAttackMs = 0.0f;
        fast.volReleaseMs = 0.0f;
        const auto rFast = render (unity, gate, 12.0, fast);
        const auto fastFall = travelTime (rFast, probe, 0.02, true);
        check (fastFall >= 0.0 && fastFall < 0.001, "zero release still closes inside 1 ms (measured "
                                                        + juce::String (fastFall * 1000.0, 3) + " ms)");

        // Tension keeps the length of the move and bends its shape: +1 races
        // away from unity and lands slowly, -1 creeps out and snaps home.
        const auto gainAfterFall = [&] (const RenderResult& r, double seconds)
        {
            for (size_t i = probe; i < r.output.size(); ++i)
                if (gainOf (r, i) < 0.99)
                    return gainOf (r, i + (size_t) (seconds * sr));

            return -1.0;
        };

        auto easedOut = slow;
        easedOut.volTension = 1.0f;
        const auto rOut = render (unity, gate, 12.0, easedOut);

        auto easedIn = slow;
        easedIn.volTension = -1.0f;
        const auto rIn = render (unity, gate, 12.0, easedIn);

        const auto midLinear = gainAfterFall (rSlow, 0.025);
        const auto midOut = gainAfterFall (rOut, 0.025);
        const auto midIn = gainAfterFall (rIn, 0.025);

        check (midOut < midLinear && midLinear < midIn,
               "tension orders the fall's midpoint (+1 " + juce::String (midOut, 3)
                   + " < linear " + juce::String (midLinear, 3)
                   + " < -1 " + juce::String (midIn, 3) + ")");

        // Measured from the gate's own close point, since tension moves where
        // the envelope crosses any single threshold but not the total length.
        const auto closeFrame = (size_t) (samplesPerLoop * (4.0 + 0.6 / 8.0));

        const auto timeToSilence = [&] (const RenderResult& r)
        {
            for (size_t i = closeFrame; i < r.output.size(); ++i)
                if (gainOf (r, i) < 0.005)
                    return (double) (i - closeFrame) / sr;

            return -1.0;
        };

        const auto fallOut = timeToSilence (rOut);
        const auto fallIn = timeToSilence (rIn);
        check (fallOut > 0.045 && fallOut < 0.056 && fallIn > 0.045 && fallIn < 0.056,
               "tension leaves the 50 ms release length alone (+1 "
                   + juce::String (fallOut * 1000.0, 1) + " ms, -1 "
                   + juce::String (fallIn * 1000.0, 1) + " ms)");
    }

    std::cout << (failures == 0 ? "all checks passed" : juce::String (failures) + " CHECK(S) FAILED")
              << std::endl;

    return failures == 0 ? 0 : 1;
}
