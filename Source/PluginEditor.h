#pragma once

#include "CurveEditor.h"
#include <juce_audio_processors/juce_audio_processors.h>

class BeatBreakProcessor;

/** A rotary slider with a caption, wired to an APVTS parameter. */
class LabelledKnob final : public juce::Component
{
public:
    LabelledKnob (juce::AudioProcessorValueTreeState& state,
                  const juce::String& paramID,
                  const juce::String& caption);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider;
    juce::String text;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

/** 36 slot buttons, laid out 12 across, Gross Beat style. */
class SlotGrid final : public juce::Component
{
public:
    SlotGrid (int numSlots, std::function<juce::String (int)> nameForSlot);

    void resized() override;
    void setSelectedSlot (int slot);

    std::function<void (int)> onSlotClicked;

private:
    juce::OwnedArray<juce::TextButton> buttons;
    int selected = -1;
};

//==============================================================================

class BeatBreakEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit BeatBreakEditor (BeatBreakProcessor&);
    ~BeatBreakEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyGridDivisions();

    BeatBreakProcessor& proc;

    CurveEditor timeEditor, volumeEditor;
    SlotGrid timeSlots, volumeSlots;

    LabelledKnob timeAmount, smoothing, volAmount, mix;

    juce::ToggleButton timeEnable { "TIME" }, volEnable { "VOLUME" }, syncButton { "Host Sync" };
    juce::ToggleButton snapButton { "Snap" };
    juce::ComboBox loopLengthBox, spanBox, gridBox;
    juce::Slider freeTempoSlider;

    juce::TextButton timeClear { "Reset" }, timeReverse { "Reverse" }, timeFactory { "Factory" };
    juce::TextButton volClear { "Reset" }, volReverse { "Reverse" }, volFactory { "Factory" };

    juce::Label titleLabel, timeSlotName, volSlotName, hintLabel;

    juce::AudioProcessorValueTreeState::ButtonAttachment timeEnableAttach, volEnableAttach, syncAttach;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment loopAttach, spanAttach;
    juce::AudioProcessorValueTreeState::SliderAttachment tempoAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatBreakEditor)
};
