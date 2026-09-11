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

/** A slot button: shows the slot's name, elided to fit; left click selects,
    right click asks for the rename menu. */
class SlotButton final : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick != nullptr)
        {
            onRightClick();
            return;
        }

        juce::TextButton::mouseDown (e);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
};

/** 36 slot buttons, laid out 12 across, Gross Beat style. */
class SlotGrid final : public juce::Component
{
public:
    SlotGrid (int numSlots, std::function<juce::String (int)> nameForSlot);

    void resized() override;
    void setSelectedSlot (int slot);

    /** Re-reads every slot name into the tooltips. */
    void refreshNames();

    std::function<void (int)> onSlotClicked;
    std::function<void (int)> onSlotRightClicked;

private:
    juce::OwnedArray<SlotButton> buttons;
    std::function<juce::String (int)> nameForSlot;
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
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void applyGridDivisions();
    void showSlotMenu (bool timeCurve, int slot);
    void renameSlot (bool timeCurve, int slot);
    void savePreset();
    void loadPreset();

    BeatBreakProcessor& proc;

    CurveEditor timeEditor, volumeEditor;
    SlotGrid timeSlots, volumeSlots;

    LabelledKnob timeAmount, smoothing, volAmount, volAttack, volRelease, volTension, mix;

    juce::ToggleButton timeEnable { "TIME" }, volEnable { "VOLUME" }, syncButton { "Host Sync" };
    juce::ToggleButton snapButton { "Snap" };
    juce::ComboBox loopLengthBox, spanBox, gridBox;
    juce::Slider freeTempoSlider;

    juce::TextButton presetSave { "Save" }, presetLoad { "Load" };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton timeClear { "Reset" }, timeReverse { "Reverse" }, timeFactory { "Factory" };
    juce::TextButton volClear { "Reset" }, volReverse { "Reverse" }, volFactory { "Factory" };

    juce::Label titleLabel, timeSlotName, volSlotName, hintLabel;

    /** Slot names only ever showed up in tooltips, which need one of these. */
    juce::TooltipWindow tooltips { this, 450 };

    juce::AudioProcessorValueTreeState::ButtonAttachment timeEnableAttach, volEnableAttach, syncAttach;
    juce::AudioProcessorValueTreeState::SliderAttachment tempoAttach;

    // Built in the constructor body: a ComboBoxAttachment can only select an
    // item that already exists, and the lists are filled there.
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> loopAttach, spanAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatBreakEditor)
};
