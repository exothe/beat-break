#pragma once

#include "CurveSnapshot.h"
#include "FactoryPatterns.h"
#include "GrossEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

class BeatBreakProcessor final : public juce::AudioProcessor,
                                 private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int numSlots = FactoryPatterns::numSlots;

    BeatBreakProcessor();
    ~BeatBreakProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BeatBreak"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Curve access. Editing happens on the message thread; call
    // publishTimeCurve / publishVolumeCurve after any change so the audio
    // thread picks it up.

    EnvelopeCurve& getTimeCurve (int slot) noexcept   { return timeCurves[(size_t) wrapSlot (slot)]; }
    EnvelopeCurve& getVolumeCurve (int slot) noexcept { return volumeCurves[(size_t) wrapSlot (slot)]; }

    int getActiveTimeSlot() const noexcept;
    int getActiveVolumeSlot() const noexcept;

    void publishActiveCurves();
    void resetSlotToFactory (bool timeCurve, int slot);

    /** Take this while mutating a curve from the message thread. */
    juce::SpinLock& getCurveLock() noexcept { return curveLock; }

    /** Playhead position inside the pattern loop, 0..1, for the editor. */
    float getLoopPhase() const noexcept  { return engine.getLoopPhase(); }
    float getCurrentGain() const noexcept { return engine.getCurrentGain(); }
    float getCurrentTimeY() const noexcept { return engine.getCurrentTimeY(); }

    /** Unity-playback line for the current loop / span setting. */
    float getUnityY (float x) const noexcept;

    juce::AudioProcessorValueTreeState apvts;

    static juce::StringArray getLoopLengthChoices()  { return { "1/4 Bar", "1/2 Bar", "1 Bar", "2 Bars", "4 Bars" }; }
    static juce::StringArray getSpanChoices()        { return { "1 Loop", "2 Loops", "4 Loops" }; }

    float getLoopBeats() const noexcept;
    float getSpanBeats() const noexcept;

    /** False when nothing is feeding us a tempo (e.g. the standalone app), in
        which case the free tempo is in use whatever Host Sync says. */
    bool hasHostTempo() const noexcept { return hostTempoAvailable.load (std::memory_order_relaxed); }

private:
    static int wrapSlot (int slot) { return juce::jlimit (0, numSlots - 1, slot); }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float newValue) override;

    std::array<EnvelopeCurve, (size_t) numSlots> timeCurves;
    std::array<EnvelopeCurve, (size_t) numSlots> volumeCurves;

    PublishedCurve liveTimeCurve, liveVolumeCurve;
    std::atomic<int> publishedTimeSlot { -1 }, publishedVolumeSlot { -1 };
    std::atomic<bool> curvesDirty { true };

    /** Guards curve editing. The editor takes it while mutating; the audio
        thread only try-locks it to take a snapshot, so it never blocks. */
    juce::SpinLock curveLock;

    GrossEngine engine;

    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* timeAmountParam = nullptr;
    std::atomic<float>* volAmountParam = nullptr;
    std::atomic<float>* smoothingParam = nullptr;
    std::atomic<float>* loopLengthParam = nullptr;
    std::atomic<float>* spanParam = nullptr;
    std::atomic<float>* timeSlotParam = nullptr;
    std::atomic<float>* volSlotParam = nullptr;
    std::atomic<float>* timeEnableParam = nullptr;
    std::atomic<float>* volEnableParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* freeTempoParam = nullptr;

    double internalPpq = 0.0;
    double beatsPerBar = 4.0;
    std::atomic<bool> hostTempoAvailable { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatBreakProcessor)
};
