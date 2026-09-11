#pragma once

#include "CurveSnapshot.h"
#include <juce_audio_basics/juce_audio_basics.h>

/**
    The audio engine.

    A ring buffer keeps the last few seconds of input. The time curve says
    which point of that buffer to read for the current position inside the
    pattern loop, so the read pointer can hold (freeze), jump backwards
    (stutter / scratch) or move at any speed (pitched slides) - exactly the
    trick Gross Beat plays. The volume curve is a synchronised gain envelope.
*/
class GrossEngine
{
public:
    struct Params
    {
        float mix = 1.0f;          // dry/wet
        float timeAmount = 1.0f;   // morph between unity playback and the curve
        float volAmount = 1.0f;    // morph between unity gain and the curve
        float smoothingMs = 0.0f;  // glide applied to the read position
        float volAttackMs = 0.0f;  // how long a rise of the volume envelope takes
        float volReleaseMs = 0.0f; // how long a fall takes
        float volTension = 0.0f;   // -1 slow start / 0 linear / +1 slow landing
        float loopBeats = 4.0f;    // pattern length
        float spanBeats = 4.0f;    // how much buffer the curve's y axis covers
        bool timeEnabled = true;
        bool volEnabled = true;
    };

    void prepare (double sampleRate, int numChannels, int maxBlockSize);
    void reset();

    /** @param ppqStart   absolute host position in beats at the block start
        @param beatsPerSample  tempo, as beats advanced per output sample */
    void process (juce::AudioBuffer<float>& buffer,
                  const Params& params,
                  const CurveSnapshot& timeCurve,
                  const CurveSnapshot& volCurve,
                  double ppqStart,
                  double beatsPerSample);

    /** Normalised position inside the pattern loop, for the editor's playhead. */
    float getLoopPhase() const noexcept { return loopPhase.load (std::memory_order_relaxed); }
    float getCurrentGain() const noexcept { return currentGain.load (std::memory_order_relaxed); }
    float getCurrentTimeY() const noexcept { return currentTimeY.load (std::memory_order_relaxed); }

    static constexpr double bufferSeconds = 24.0;

private:
    float readInterpolated (int channel, float delaySamples) const noexcept;
    void pushSample (int channel, float value) noexcept;

    juce::AudioBuffer<float> ring;
    int ringSize = 0;
    int writePos = 0;
    double sr = 44100.0;

    float smoothedDelay = 0.0f;
    float previousDelay = 0.0f;
    bool delayPrimed = false;

    // Click suppression: on a jump we keep playing the old read position at
    // unity speed (constant delay) and crossfade into the new one.
    float fadeFromDelay = 0.0f;
    int fadeSamplesLeft = 0;
    int fadeLength = 0;

    float smoothedGain = 1.0f;

    std::atomic<float> loopPhase { 0.0f };
    std::atomic<float> currentGain { 1.0f };
    std::atomic<float> currentTimeY { 0.0f };
};
