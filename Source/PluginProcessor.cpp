#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ParamID
{
    static const juce::String mix         { "mix" };
    static const juce::String timeAmount  { "timeAmount" };
    static const juce::String volAmount   { "volAmount" };
    static const juce::String smoothing   { "smoothing" };
    static const juce::String loopLength  { "loopLength" };
    static const juce::String span        { "span" };
    static const juce::String timeSlot    { "timeSlot" };
    static const juce::String volSlot     { "volSlot" };
    static const juce::String timeEnable  { "timeEnable" };
    static const juce::String volEnable   { "volEnable" };
    static const juce::String sync        { "sync" };
    static const juce::String freeTempo   { "freeTempo" };
}

//==============================================================================

BeatBreakProcessor::BeatBreakProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout())
{
    for (int i = 0; i < numSlots; ++i)
    {
        timeCurves[(size_t) i]   = FactoryPatterns::getTimeCurve (i);
        volumeCurves[(size_t) i] = FactoryPatterns::getVolumeCurve (i);
    }

    mixParam        = apvts.getRawParameterValue (ParamID::mix);
    timeAmountParam = apvts.getRawParameterValue (ParamID::timeAmount);
    volAmountParam  = apvts.getRawParameterValue (ParamID::volAmount);
    smoothingParam  = apvts.getRawParameterValue (ParamID::smoothing);
    loopLengthParam = apvts.getRawParameterValue (ParamID::loopLength);
    spanParam       = apvts.getRawParameterValue (ParamID::span);
    timeSlotParam   = apvts.getRawParameterValue (ParamID::timeSlot);
    volSlotParam    = apvts.getRawParameterValue (ParamID::volSlot);
    timeEnableParam = apvts.getRawParameterValue (ParamID::timeEnable);
    volEnableParam  = apvts.getRawParameterValue (ParamID::volEnable);
    syncParam       = apvts.getRawParameterValue (ParamID::sync);
    freeTempoParam  = apvts.getRawParameterValue (ParamID::freeTempo);

    apvts.addParameterListener (ParamID::timeSlot, this);
    apvts.addParameterListener (ParamID::volSlot, this);
}

BeatBreakProcessor::~BeatBreakProcessor()
{
    apvts.removeParameterListener (ParamID::timeSlot, this);
    apvts.removeParameterListener (ParamID::volSlot, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout BeatBreakProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percent = [] (float value, int) { return String (roundToInt (value * 100.0f)) + " %"; };

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::mix, 1 }, "Mix",
                                                       NormalisableRange<float> (0.0f, 1.0f), 1.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (percent)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::timeAmount, 1 }, "Time Amount",
                                                       NormalisableRange<float> (0.0f, 1.0f), 1.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (percent)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::volAmount, 1 }, "Volume Amount",
                                                       NormalisableRange<float> (0.0f, 1.0f), 1.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (percent)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::smoothing, 1 }, "Smoothing",
                                                       NormalisableRange<float> (0.0f, 200.0f, 0.1f, 0.4f), 0.0f,
                                                       AudioParameterFloatAttributes()
                                                           .withStringFromValueFunction ([] (float v, int)
                                                                                         { return String (v, 1) + " ms"; })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::loopLength, 1 }, "Loop Length",
                                                        getLoopLengthChoices(), 2));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::span, 1 }, "Time Span",
                                                        getSpanChoices(), 0));

    StringArray slotNames;
    for (int i = 0; i < numSlots; ++i)
        slotNames.add (String (i + 1));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::timeSlot, 1 }, "Time Slot",
                                                        slotNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::volSlot, 1 }, "Volume Slot",
                                                        slotNames, 0));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::timeEnable, 1 }, "Time On", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::volEnable, 1 }, "Volume On", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::sync, 1 }, "Host Sync", true));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::freeTempo, 1 }, "Free Tempo",
                                                       NormalisableRange<float> (40.0f, 250.0f, 0.01f), 120.0f,
                                                       AudioParameterFloatAttributes()
                                                           .withStringFromValueFunction ([] (float v, int)
                                                                                         { return String (v, 2) + " BPM"; })));

    return layout;
}

void BeatBreakProcessor::parameterChanged (const juce::String&, float)
{
    // Slot switches are handled in processBlock, which owns snapshot
    // publishing; nothing to do here beyond waking that check up.
    curvesDirty.store (true, std::memory_order_release);
}

//==============================================================================

void BeatBreakProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()),
                    samplesPerBlock);

    publishedTimeSlot.store (-1);
    publishedVolumeSlot.store (-1);
    curvesDirty.store (true);
    internalPpq = 0.0;
}

void BeatBreakProcessor::releaseResources()
{
    engine.reset();
}

bool BeatBreakProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

int BeatBreakProcessor::getActiveTimeSlot() const noexcept
{
    return wrapSlot ((int) timeSlotParam->load());
}

int BeatBreakProcessor::getActiveVolumeSlot() const noexcept
{
    return wrapSlot ((int) volSlotParam->load());
}

float BeatBreakProcessor::getLoopBeats() const noexcept
{
    static const float bars[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    const auto index = juce::jlimit (0, 4, (int) loopLengthParam->load());
    return bars[index] * (float) beatsPerBar;
}

float BeatBreakProcessor::getSpanBeats() const noexcept
{
    static const float loops[] = { 1.0f, 2.0f, 4.0f };
    const auto index = juce::jlimit (0, 2, (int) spanParam->load());
    return getLoopBeats() * loops[index];
}

float BeatBreakProcessor::getUnityY (float x) const noexcept
{
    const auto loopBeats = getLoopBeats();
    const auto spanBeats = juce::jmax (1.0e-3f, getSpanBeats());
    return juce::jlimit (0.0f, 1.0f, 1.0f - loopBeats * (1.0f - x) / spanBeats);
}

void BeatBreakProcessor::publishActiveCurves()
{
    curvesDirty.store (true, std::memory_order_release);
}

void BeatBreakProcessor::resetSlotToFactory (bool timeCurve, int slot)
{
    slot = wrapSlot (slot);

    const juce::SpinLock::ScopedLockType lock (curveLock);

    if (timeCurve)
        timeCurves[(size_t) slot] = FactoryPatterns::getTimeCurve (slot);
    else
        volumeCurves[(size_t) slot] = FactoryPatterns::getVolumeCurve (slot);

    curvesDirty.store (true, std::memory_order_release);
}

//==============================================================================

void BeatBreakProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples <= 0)
        return;

    // ---- tempo / transport -------------------------------------------------
    auto bpm = (double) freeTempoParam->load();
    auto hostSyncing = syncParam->load() > 0.5f;
    auto ppq = internalPpq;
    auto usingHostPosition = false;

    auto tempoFromHost = false;

    if (auto* playHead = getPlayHead())
    {
        if (const auto pos = playHead->getPosition())
        {
            if (const auto hostBpm = pos->getBpm())
                if (*hostBpm > 1.0)
                {
                    tempoFromHost = true;

                    if (hostSyncing)
                        bpm = *hostBpm;
                }

            if (const auto sig = pos->getTimeSignature())
                if (sig->numerator > 0 && sig->denominator > 0)
                    beatsPerBar = (double) sig->numerator * 4.0 / (double) sig->denominator;

            if (hostSyncing && pos->getIsPlaying())
            {
                if (const auto hostPpq = pos->getPpqPosition())
                {
                    ppq = *hostPpq;
                    usingHostPosition = true;
                }
            }
        }
    }

    hostTempoAvailable.store (tempoFromHost, std::memory_order_relaxed);

    const auto beatsPerSample = bpm / 60.0 / juce::jmax (1.0, getSampleRate());

    // ---- publish any curve edits / slot changes ---------------------------
    const auto timeSlot = getActiveTimeSlot();
    const auto volSlot = getActiveVolumeSlot();

    if (curvesDirty.load (std::memory_order_acquire)
        || publishedTimeSlot.load() != timeSlot
        || publishedVolumeSlot.load() != volSlot)
    {
        const juce::SpinLock::ScopedTryLockType lock (curveLock);

        if (lock.isLocked())
        {
            liveTimeCurve.publish (timeCurves[(size_t) timeSlot]);
            liveVolumeCurve.publish (volumeCurves[(size_t) volSlot]);
            publishedTimeSlot.store (timeSlot);
            publishedVolumeSlot.store (volSlot);
            curvesDirty.store (false, std::memory_order_release);
        }
        // If the editor holds the lock we simply keep last block's snapshot.
    }

    // ---- run ---------------------------------------------------------------
    GrossEngine::Params params;
    params.mix = mixParam->load();
    params.timeAmount = timeAmountParam->load();
    params.volAmount = volAmountParam->load();
    params.smoothingMs = smoothingParam->load();
    params.loopBeats = getLoopBeats();
    params.spanBeats = getSpanBeats();
    params.timeEnabled = timeEnableParam->load() > 0.5f;
    params.volEnabled = volEnableParam->load() > 0.5f;

    engine.process (buffer, params, liveTimeCurve.get(), liveVolumeCurve.get(), ppq, beatsPerSample);

    internalPpq = (usingHostPosition ? ppq : internalPpq) + beatsPerSample * (double) numSamples;
}

//==============================================================================

void BeatBreakProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // Curves live alongside the parameters as plain text attributes.
    state.removeChild (state.getChildWithName ("curves"), nullptr);
    juce::ValueTree curves ("curves");

    {
        const juce::SpinLock::ScopedLockType lock (curveLock);

        for (int i = 0; i < numSlots; ++i)
        {
            curves.setProperty ("t" + juce::String (i), timeCurves[(size_t) i].toString(), nullptr);
            curves.setProperty ("v" + juce::String (i), volumeCurves[(size_t) i].toString(), nullptr);
        }
    }

    state.appendChild (curves, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BeatBreakProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid() || state.getType() != apvts.state.getType())
        return;

    auto curves = state.getChildWithName ("curves");

    if (curves.isValid())
    {
        const juce::SpinLock::ScopedLockType lock (curveLock);

        for (int i = 0; i < numSlots; ++i)
        {
            const auto timeText = curves.getProperty ("t" + juce::String (i)).toString();
            const auto volText  = curves.getProperty ("v" + juce::String (i)).toString();

            if (timeText.isNotEmpty())
                timeCurves[(size_t) i] = EnvelopeCurve::fromString (timeText);

            if (volText.isNotEmpty())
                volumeCurves[(size_t) i] = EnvelopeCurve::fromString (volText);
        }
    }

    apvts.replaceState (state);
    curvesDirty.store (true, std::memory_order_release);

    if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (getActiveEditor()))
        editor->repaint();
}

//==============================================================================

juce::AudioProcessorEditor* BeatBreakProcessor::createEditor()
{
    return new BeatBreakEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BeatBreakProcessor();
}
