#include "GrossEngine.h"

void GrossEngine::prepare (double sampleRate, int numChannels, int /*maxBlockSize*/)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    ringSize = juce::jmax (1024, (int) std::ceil (sr * bufferSeconds));
    ring.setSize (juce::jmax (1, numChannels), ringSize, false, true, true);
    reset();
}

void GrossEngine::reset()
{
    ring.clear();
    writePos = 0;
    smoothedDelay = 0.0f;
    previousDelay = 0.0f;
    delayPrimed = false;
    fadeSamplesLeft = 0;
    fadeLength = juce::jmax (16, (int) (sr * 0.003));   // 3 ms declick
    smoothedGain = 1.0f;
}

void GrossEngine::pushSample (int channel, float value) noexcept
{
    ring.getWritePointer (channel)[writePos] = value;
}

float GrossEngine::readInterpolated (int channel, float delaySamples) const noexcept
{
    const auto* data = ring.getReadPointer (channel);

    // The newest sample sits at writePos, so zero delay is a pass-through.
    const auto pos = (float) writePos - delaySamples;
    auto i = (int) std::floor (pos);
    const auto frac = pos - (float) i;

    const auto wrap = [this] (int index)
    {
        index %= ringSize;
        return index < 0 ? index + ringSize : index;
    };

    if (delaySamples < 2.0f)
    {
        // Not enough written history ahead of the read point for a cubic
        // kernel: fall back to linear between the two newest samples.
        const auto a = data[wrap (i)];
        const auto b = data[wrap (i + 1)];
        return a + (b - a) * frac;
    }

    const auto y0 = data[wrap (i - 1)];
    const auto y1 = data[wrap (i)];
    const auto y2 = data[wrap (i + 1)];
    const auto y3 = data[wrap (i + 2)];

    // Catmull-Rom: smooth enough for pitched slides, cheap enough per sample.
    const auto c0 = y1;
    const auto c1 = 0.5f * (y2 - y0);
    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

void GrossEngine::process (juce::AudioBuffer<float>& buffer,
                           const Params& params,
                           const CurveSnapshot& timeCurve,
                           const CurveSnapshot& volCurve,
                           double ppqStart,
                           double beatsPerSample)
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = juce::jmin (buffer.getNumChannels(), ring.getNumChannels());

    if (numChannels <= 0 || ringSize <= 0)
        return;

    const auto loopBeats = juce::jmax (0.03125, (double) params.loopBeats);
    const auto spanBeats = juce::jmax (0.03125, (double) params.spanBeats);
    const auto samplesPerBeat = beatsPerSample > 1.0e-12 ? 1.0 / beatsPerSample : sr;
    const auto maxDelay = (float) (ringSize - 8);

    // ---- volume envelope -------------------------------------------------
    // Attack and release are the time a full-scale (0 -> 1) move takes, so the
    // envelope is a slew limiter: slower ramps in the curve pass through
    // untouched, steps get shaped. The 0.2 ms floor is what stops a hard step
    // from clicking when both knobs are at zero.
    const auto envStep = [this] (float milliseconds)
    {
        const auto seconds = juce::jmax (0.0002, (double) milliseconds * 0.001);
        return (float) (1.0 / (seconds * sr));
    };

    const auto attackStep = envStep (params.volAttackMs);
    const auto releaseStep = envStep (params.volReleaseMs);

    // Tension bends the shape of the move without changing how long it takes:
    // with a rate of k * travelled^(1 - 1/k) the full-scale trip still
    // integrates to exactly the knob's time for any k. k < 1 races away and
    // lands slowly (exponential-sounding), k > 1 creeps out and snaps home.
    const auto tension = juce::jlimit (-1.0f, 1.0f, params.volTension);
    const auto shaped = std::abs (tension) > 1.0e-4f;
    const auto k = std::pow (2.0f, -tension * 2.0f);
    const auto bendExponent = 1.0f - 1.0f / k;

    const auto smoothingSeconds = (double) params.smoothingMs * 0.001;
    const auto delayCoeff = smoothingSeconds > 1.0e-5
                              ? (float) (1.0 - std::exp (-1.0 / (smoothingSeconds * sr)))
                              : 1.0f;

    const auto jumpThreshold = (float) (sr * 0.002);   // 2 ms of movement per sample

    auto ppq = ppqStart;

    for (int n = 0; n < numSamples; ++n)
    {
        // ---- write the incoming block into the ring ----------------------
        for (int ch = 0; ch < numChannels; ++ch)
            pushSample (ch, buffer.getReadPointer (ch)[n]);

        // ---- position inside the pattern loop ----------------------------
        const auto loops = ppq / loopBeats;
        auto x = (float) (loops - std::floor (loops));
        if (! std::isfinite (x))
            x = 0.0f;

        // ---- time curve -> read position ---------------------------------
        // The curve's y axis covers spanBeats of the buffer, with the top of
        // the grid at the end of the current loop. Unity playback is the line
        // y = 1 - loopBeats * (1 - x) / spanBeats, which is the corner to
        // corner diagonal whenever spanBeats == loopBeats.
        const auto unityY = (float) (1.0 - loopBeats * (1.0 - x) / spanBeats);
        auto y = unityY;

        if (params.timeEnabled)
        {
            const auto curveY = timeCurve.getValue (x);
            y = unityY + (curveY - unityY) * params.timeAmount;
        }

        // delay = now - mapped position, in beats
        const auto delayBeats = (double) x * loopBeats - loopBeats + (1.0 - (double) y) * spanBeats;
        auto targetDelay = (float) juce::jlimit (0.0, (double) maxDelay, delayBeats * samplesPerBeat);

        if (! delayPrimed)
        {
            smoothedDelay = targetDelay;
            previousDelay = targetDelay;
            delayPrimed = true;
        }

        smoothedDelay += (targetDelay - smoothedDelay) * delayCoeff;
        auto effectiveDelay = juce::jlimit (0.0f, maxDelay, smoothedDelay);

        // ---- declick on discontinuities ----------------------------------
        if (std::abs (effectiveDelay - previousDelay) > jumpThreshold)
        {
            if (fadeSamplesLeft <= 0)
                fadeFromDelay = previousDelay;   // keep the old reader rolling

            fadeSamplesLeft = fadeLength;
        }

        previousDelay = effectiveDelay;

        // ---- volume curve -------------------------------------------------
        auto targetGain = 1.0f;
        if (params.volEnabled)
        {
            const auto curveGain = volCurve.getValue (x);
            targetGain = 1.0f + (curveGain - 1.0f) * params.volAmount;
        }

        const auto diff = targetGain - smoothedGain;
        const auto distance = std::abs (diff);

        if (distance > 1.0e-7f)
        {
            auto step = diff > 0.0f ? attackStep : releaseStep;

            if (shaped)
            {
                // How far through a full-scale move we already are.
                const auto travelled = juce::jlimit (1.0e-6f, 1.0f, 1.0f - distance);
                step *= juce::jlimit (0.002f, 64.0f, k * std::pow (travelled, bendExponent));
            }

            smoothedGain += juce::jmin (step, distance) * (diff > 0.0f ? 1.0f : -1.0f);
        }
        else
        {
            smoothedGain = targetGain;
        }

        // ---- render --------------------------------------------------------
        const auto fadeMix = fadeSamplesLeft > 0
                               ? (float) (fadeLength - fadeSamplesLeft) / (float) fadeLength
                               : 1.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto dry = buffer.getReadPointer (ch)[n];
            auto wet = readInterpolated (ch, effectiveDelay);

            if (fadeSamplesLeft > 0)
            {
                const auto old = readInterpolated (ch, fadeFromDelay);
                wet = old + (wet - old) * fadeMix;
            }

            wet *= smoothedGain;
            buffer.getWritePointer (ch)[n] = dry + (wet - dry) * params.mix;
        }

        if (fadeSamplesLeft > 0)
            --fadeSamplesLeft;

        writePos = (writePos + 1) % ringSize;
        ppq += beatsPerSample;

        if (n == numSamples - 1)
        {
            loopPhase.store (x, std::memory_order_relaxed);
            currentGain.store (smoothedGain, std::memory_order_relaxed);
            currentTimeY.store (y, std::memory_order_relaxed);
        }
    }
}
