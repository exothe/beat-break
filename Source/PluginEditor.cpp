#include "PluginEditor.h"
#include "FactoryPatterns.h"
#include "PluginProcessor.h"

namespace
{
    const juce::Colour panelColour { 0xff1b1e27 };
    const juce::Colour backColour  { 0xff0f1117 };
    const juce::Colour accentTime  { 0xff4ad0ff };
    const juce::Colour accentVol   { 0xffffc14a };
}

//==============================================================================

LabelledKnob::LabelledKnob (juce::AudioProcessorValueTreeState& state,
                            const juce::String& paramID,
                            const juce::String& caption)
    : text (caption), attachment (state, paramID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 16);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);
}

void LabelledKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (14);
    slider.setBounds (r);
}

void LabelledKnob::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (text, getLocalBounds().removeFromTop (14), juce::Justification::centred);
}

//==============================================================================

SlotGrid::SlotGrid (int numSlots, std::function<juce::String (int)> nameForSlot)
{
    for (int i = 0; i < numSlots; ++i)
    {
        auto* b = buttons.add (new juce::TextButton (juce::String (i + 1)));
        b->setTooltip (nameForSlot != nullptr ? nameForSlot (i) : juce::String());
        b->setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight
                              | juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        b->onClick = [this, i]
        {
            if (onSlotClicked != nullptr)
                onSlotClicked (i);
        };
        addAndMakeVisible (b);
    }
}

void SlotGrid::resized()
{
    const auto columns = 12;
    const auto rows = juce::jmax (1, (buttons.size() + columns - 1) / columns);
    const auto w = (float) getWidth() / (float) columns;
    const auto h = (float) getHeight() / (float) rows;

    for (int i = 0; i < buttons.size(); ++i)
    {
        const auto col = i % columns;
        const auto row = i / columns;
        buttons[i]->setBounds (juce::Rectangle<float> ((float) col * w, (float) row * h, w, h)
                                   .toNearestInt().reduced (1));
    }
}

void SlotGrid::setSelectedSlot (int slot)
{
    if (slot == selected)
        return;

    selected = slot;

    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setColour (juce::TextButton::buttonColourId,
                               i == slot ? juce::Colour (0xff3d6f8f) : juce::Colour (0xff262a35));
}

//==============================================================================

BeatBreakEditor::BeatBreakEditor (BeatBreakProcessor& p)
    : juce::AudioProcessorEditor (&p),
      proc (p),
      timeEditor (p, CurveEditor::Mode::time),
      volumeEditor (p, CurveEditor::Mode::volume),
      timeSlots (BeatBreakProcessor::numSlots, [] (int i) { return FactoryPatterns::getTimeName (i); }),
      volumeSlots (BeatBreakProcessor::numSlots, [] (int i) { return FactoryPatterns::getVolumeName (i); }),
      timeAmount (p.apvts, "timeAmount", "TIME AMT"),
      smoothing (p.apvts, "smoothing", "SMOOTH"),
      volAmount (p.apvts, "volAmount", "VOL AMT"),
      mix (p.apvts, "mix", "MIX"),
      timeEnableAttach (p.apvts, "timeEnable", timeEnable),
      volEnableAttach (p.apvts, "volEnable", volEnable),
      syncAttach (p.apvts, "sync", syncButton),
      loopAttach (p.apvts, "loopLength", loopLengthBox),
      spanAttach (p.apvts, "span", spanBox),
      tempoAttach (p.apvts, "freeTempo", freeTempoSlider)
{
    setSize (1000, 700);
    setResizable (true, true);
    setResizeLimits (860, 600, 1800, 1200);

    titleLabel.setText ("BEATBREAK", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
    addAndMakeVisible (titleLabel);

    hintLabel.setText ("double-click: add / remove point   drag segment or wheel: bend   "
                       "right-click point: step / smooth   shift: no snap",
                       juce::dontSendNotification);
    hintLabel.setFont (juce::FontOptions (11.0f));
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.4f));
    addAndMakeVisible (hintLabel);

    for (auto* l : { &timeSlotName, &volSlotName })
    {
        l->setFont (juce::FontOptions (13.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }

    loopLengthBox.addItemList (BeatBreakProcessor::getLoopLengthChoices(), 1);
    spanBox.addItemList (BeatBreakProcessor::getSpanChoices(), 1);
    addAndMakeVisible (loopLengthBox);
    addAndMakeVisible (spanBox);

    gridBox.addItemList ({ "1/4", "1/8", "1/12", "1/16", "1/24", "1/32" }, 1);
    gridBox.setSelectedId (4, juce::dontSendNotification);   // 1/16
    gridBox.onChange = [this] { applyGridDivisions(); };
    addAndMakeVisible (gridBox);

    snapButton.setToggleState (true, juce::dontSendNotification);
    snapButton.onClick = [this]
    {
        timeEditor.setSnapEnabled (snapButton.getToggleState());
        volumeEditor.setSnapEnabled (snapButton.getToggleState());
    };
    addAndMakeVisible (snapButton);

    freeTempoSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    freeTempoSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 74, 18);
    addAndMakeVisible (freeTempoSlider);

    timeEnable.setColour (juce::ToggleButton::textColourId, accentTime);
    volEnable.setColour (juce::ToggleButton::textColourId, accentVol);
    addAndMakeVisible (timeEnable);
    addAndMakeVisible (volEnable);
    addAndMakeVisible (syncButton);

    addAndMakeVisible (timeEditor);
    addAndMakeVisible (volumeEditor);
    addAndMakeVisible (timeSlots);
    addAndMakeVisible (volumeSlots);
    addAndMakeVisible (timeAmount);
    addAndMakeVisible (smoothing);
    addAndMakeVisible (volAmount);
    addAndMakeVisible (mix);

    timeSlots.onSlotClicked = [this] (int slot)
    {
        if (auto* param = proc.apvts.getParameter ("timeSlot"))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) slot));
    };

    volumeSlots.onSlotClicked = [this] (int slot)
    {
        if (auto* param = proc.apvts.getParameter ("volSlot"))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) slot));
    };

    const auto styleSmall = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff262a35));
    };

    for (auto* b : { &timeClear, &timeReverse, &timeFactory, &volClear, &volReverse, &volFactory })
    {
        styleSmall (*b);
        addAndMakeVisible (b);
    }

    timeClear.onClick    = [this] { timeEditor.clearCurve(); };
    timeReverse.onClick  = [this] { timeEditor.reverseCurve(); };
    timeFactory.onClick  = [this] { timeEditor.resetToFactory(); };
    volClear.onClick     = [this] { volumeEditor.clearCurve(); };
    volReverse.onClick   = [this] { volumeEditor.reverseCurve(); };
    volFactory.onClick   = [this] { volumeEditor.resetToFactory(); };

    applyGridDivisions();
    timerCallback();
    startTimerHz (15);
}

BeatBreakEditor::~BeatBreakEditor()
{
    stopTimer();
}

void BeatBreakEditor::applyGridDivisions()
{
    static const int divisions[] = { 4, 8, 12, 16, 24, 32 };
    const auto index = juce::jlimit (1, 6, gridBox.getSelectedId()) - 1;

    timeEditor.setGridDivisions (divisions[index]);
    volumeEditor.setGridDivisions (divisions[index]);
}

void BeatBreakEditor::timerCallback()
{
    const auto t = proc.getActiveTimeSlot();
    const auto v = proc.getActiveVolumeSlot();

    timeSlots.setSelectedSlot (t);
    volumeSlots.setSelectedSlot (v);

    timeSlotName.setText (FactoryPatterns::getTimeName (t), juce::dontSendNotification);
    volSlotName.setText (FactoryPatterns::getVolumeName (v), juce::dontSendNotification);

    // With no host tempo (the standalone app) the free tempo is what actually
    // drives the engine, so leave it editable even when Host Sync is on.
    const auto syncing = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    freeTempoSlider.setEnabled (! syncing || ! proc.hasHostTempo());
}

//==============================================================================

void BeatBreakEditor::paint (juce::Graphics& g)
{
    g.fillAll (backColour);

    const auto panel = [&g] (juce::Rectangle<int> r, juce::Colour accent)
    {
        g.setColour (panelColour);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (accent.withAlpha (0.25f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
    };

    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (46);

    const auto halfHeight = area.getHeight() / 2;
    panel (area.removeFromTop (halfHeight).reduced (0, 4), accentTime);
    panel (area.reduced (0, 4), accentVol);
}

void BeatBreakEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    // ---- header -------------------------------------------------------------
    auto header = area.removeFromTop (46);
    titleLabel.setBounds (header.removeFromLeft (130).withTrimmedTop (6));

    header.removeFromRight (4);
    mix.setBounds (header.removeFromRight (74));
    header.removeFromRight (10);

    auto controls = header.withTrimmedTop (10).withTrimmedBottom (6);
    loopLengthBox.setBounds (controls.removeFromLeft (96).reduced (2, 0));
    spanBox.setBounds (controls.removeFromLeft (86).reduced (2, 0));
    gridBox.setBounds (controls.removeFromLeft (70).reduced (2, 0));
    snapButton.setBounds (controls.removeFromLeft (70).reduced (2, 0));
    syncButton.setBounds (controls.removeFromLeft (94).reduced (2, 0));
    freeTempoSlider.setBounds (controls.removeFromLeft (200).reduced (2, 0));
    hintLabel.setBounds (controls.reduced (6, 0));

    // ---- the two halves -----------------------------------------------------
    const auto halfHeight = area.getHeight() / 2;

    const auto layoutSection = [] (juce::Rectangle<int> section,
                                   juce::ToggleButton& enable,
                                   juce::Label& name,
                                   SlotGrid& slots,
                                   CurveEditor& editor,
                                   std::vector<juce::Component*> knobs,
                                   std::vector<juce::TextButton*> buttons)
    {
        section = section.reduced (10, 12);

        auto top = section.removeFromTop (22);
        enable.setBounds (top.removeFromLeft (110));
        name.setBounds (top.removeFromRight (200));

        section.removeFromTop (6);
        slots.setBounds (section.removeFromTop (52));
        section.removeFromTop (8);

        auto side = section.removeFromRight (96);
        for (auto* knob : knobs)
        {
            knob->setBounds (side.removeFromTop (78));
            side.removeFromTop (6);
        }

        for (auto* b : buttons)
        {
            b->setBounds (side.removeFromTop (24).reduced (4, 2));
            side.removeFromTop (2);
        }

        editor.setBounds (section.withTrimmedRight (8));
    };

    layoutSection (area.removeFromTop (halfHeight), timeEnable, timeSlotName, timeSlots, timeEditor,
                   { &timeAmount, &smoothing }, { &timeClear, &timeReverse, &timeFactory });

    layoutSection (area, volEnable, volSlotName, volumeSlots, volumeEditor,
                   { &volAmount }, { &volClear, &volReverse, &volFactory });
}
