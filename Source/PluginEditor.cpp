#include "PluginEditor.h"
#include "FactoryPatterns.h"
#include "PluginProcessor.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

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

namespace
{
    /** Trims text to fit, ending in an ellipsis. The buttons are far narrower
        than the pattern names, so drawFittedText would squash them instead. */
    juce::String elideToWidth (const juce::String& text, const juce::Font& font, float maxWidth)
    {
        if (text.isEmpty() || juce::GlyphArrangement::getStringWidth (font, text) <= maxWidth)
            return text;

        const auto dots = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
        const auto room = maxWidth - juce::GlyphArrangement::getStringWidth (font, dots);

        if (room <= 0.0f)
            return {};

        auto fitting = text;

        while (fitting.isNotEmpty()
               && juce::GlyphArrangement::getStringWidth (font, fitting) > room)
            fitting = fitting.dropLastCharacters (1);

        return fitting.trimEnd() + dots;
    }
}

void SlotButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto& lf = getLookAndFeel();

    lf.drawButtonBackground (g, *this,
                             findColour (getToggleState() ? juce::TextButton::buttonOnColourId
                                                          : juce::TextButton::buttonColourId),
                             highlighted, down);

    const auto area = getLocalBounds().reduced (3, 1);
    const juce::Font font (juce::FontOptions (juce::jlimit (8.5f, 13.0f, (float) getHeight() * 0.62f)));

    g.setFont (font);
    g.setColour (findColour (getToggleState() ? juce::TextButton::textColourOnId
                                              : juce::TextButton::textColourOffId));
    g.drawText (elideToWidth (getButtonText(), font, (float) area.getWidth()),
                area, juce::Justification::centred, false);
}

//==============================================================================

SlotGrid::SlotGrid (int numSlots, std::function<juce::String (int)> nameSource)
    : nameForSlot (std::move (nameSource))
{
    for (int i = 0; i < numSlots; ++i)
    {
        auto* b = buttons.add (new SlotButton());
        b->setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight
                              | juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        b->onClick = [this, i]
        {
            if (onSlotClicked != nullptr)
                onSlotClicked (i);
        };
        b->onRightClick = [this, i]
        {
            if (onSlotRightClicked != nullptr)
                onSlotRightClicked (i);
        };
        addAndMakeVisible (b);
    }

    refreshNames();
}

void SlotGrid::refreshNames()
{
    if (nameForSlot == nullptr)
        return;

    for (int i = 0; i < buttons.size(); ++i)
    {
        const auto name = nameForSlot (i);

        if (buttons[i]->getButtonText() != name)
            buttons[i]->setButtonText (name);

        // The tooltip carries the slot number and the name in full, since the
        // button itself only has room for a few characters of it.
        const auto tooltip = juce::String (i + 1) + "  -  " + name;

        if (buttons[i]->getTooltip() != tooltip)
            buttons[i]->setTooltip (tooltip);
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
      timeSlots (BeatBreakProcessor::numSlots, [&p] (int i) { return p.getSlotName (true, i); }),
      volumeSlots (BeatBreakProcessor::numSlots, [&p] (int i) { return p.getSlotName (false, i); }),
      timeAmount (p.apvts, "timeAmount", "TIME AMT"),
      smoothing (p.apvts, "smoothing", "SMOOTH"),
      volAmount (p.apvts, "volAmount", "VOL AMT"),
      volAttack (p.apvts, "volAttack", "ATT"),
      volRelease (p.apvts, "volRelease", "REL"),
      volTension (p.apvts, "volTension", "TENSION"),
      mix (p.apvts, "mix", "MIX"),
      timeEnableAttach (p.apvts, "timeEnable", timeEnable),
      volEnableAttach (p.apvts, "volEnable", volEnable),
      syncAttach (p.apvts, "sync", syncButton),
      tempoAttach (p.apvts, "freeTempo", freeTempoSlider)
{
    setSize (1000, 700);
    setResizable (true, true);
    setResizeLimits (860, 600, 1800, 1200);

    titleLabel.setText ("BEATBREAK", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
    addAndMakeVisible (titleLabel);

    hintLabel.setText ("drag point: move   drag segment, handle or wheel: bend   "
                       "right-click: add / move point   right-click point: step / smooth   "
                       "double-click point: remove   right-click handle: reset bend   "
                       "right-click slot: rename   shift: no snap",
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

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    loopAttach = std::make_unique<ComboAttachment> (p.apvts, "loopLength", loopLengthBox);
    spanAttach = std::make_unique<ComboAttachment> (p.apvts, "span", spanBox);

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
    addAndMakeVisible (volAttack);
    addAndMakeVisible (volRelease);
    addAndMakeVisible (volTension);
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

    timeSlots.onSlotRightClicked   = [this] (int slot) { showSlotMenu (true, slot); };
    volumeSlots.onSlotRightClicked = [this] (int slot) { showSlotMenu (false, slot); };

    presetSave.onClick = [this] { savePreset(); };
    presetLoad.onClick = [this] { loadPreset(); };

    const auto styleSmall = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff262a35));
    };

    for (auto* b : { &presetSave, &presetLoad, &timeClear, &timeReverse, &timeFactory,
                     &volClear, &volReverse, &volFactory })
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

    // Hosts own the keyboard: FL Studio's spacebar starts the transport, but a
    // JUCE slider or button takes focus when clicked and then eats the key
    // (Button treats space as a click). Nothing here needs typing - renaming
    // happens in its own window - so no part of the editor takes focus.
    const std::function<void (juce::Component&)> dropKeyboardFocus =
        [&dropKeyboardFocus] (juce::Component& c)
        {
            c.setWantsKeyboardFocus (false);
            c.setMouseClickGrabsKeyboardFocus (false);

            for (auto* child : c.getChildren())
                dropKeyboardFocus (*child);
        };

    dropKeyboardFocus (*this);

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

void BeatBreakEditor::showSlotMenu (bool timeCurve, int slot)
{
    const auto factoryName = timeCurve ? FactoryPatterns::getTimeName (slot)
                                       : FactoryPatterns::getVolumeName (slot);
    const auto renamed = proc.getSlotName (timeCurve, slot) != factoryName;

    juce::PopupMenu menu;
    menu.addSectionHeader (proc.getSlotName (timeCurve, slot));
    menu.addItem (1, "Rename...");
    menu.addItem (2, "Use factory name (" + factoryName + ")", renamed);

    menu.showMenuAsync (juce::PopupMenu::Options(), [this, timeCurve, slot] (int result)
    {
        if (result == 1)
            renameSlot (timeCurve, slot);
        else if (result == 2)
            proc.setSlotName (timeCurve, slot, {});
    });
}

void BeatBreakEditor::renameSlot (bool timeCurve, int slot)
{
    auto* window = new juce::AlertWindow (timeCurve ? "Rename time slot " + juce::String (slot + 1)
                                                    : "Rename volume slot " + juce::String (slot + 1),
                                          "An empty name puts the factory one back.",
                                          juce::MessageBoxIconType::NoIcon);

    window->addTextEditor ("name", proc.getSlotName (timeCurve, slot), "Name");
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window, timeCurve, slot] (int result)
        {
            if (result == 1)
            {
                auto name = window->getTextEditorContents ("name");

                // A name that just repeats the factory one is not a rename.
                const auto factoryName = timeCurve ? FactoryPatterns::getTimeName (slot)
                                                   : FactoryPatterns::getVolumeName (slot);
                proc.setSlotName (timeCurve, slot, name.trim() == factoryName ? juce::String() : name);
            }

            delete window;
        }), false);
}

void BeatBreakEditor::savePreset()
{
    const auto directory = BeatBreakProcessor::getPresetDirectory();
    directory.createDirectory();

    chooser = std::make_unique<juce::FileChooser> ("Save preset",
                                                   directory.getChildFile ("Preset" + BeatBreakProcessor::getPresetExtension()),
                                                   "*" + BeatBreakProcessor::getPresetExtension());

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();

                              if (file == juce::File())
                                  return;

                              if (! file.hasFileExtension (BeatBreakProcessor::getPresetExtension()))
                                  file = file.withFileExtension (BeatBreakProcessor::getPresetExtension());

                              if (! proc.savePreset (file))
                                  juce::NativeMessageBox::showAsync (
                                      juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("BeatBreak")
                                          .withMessage ("Could not write " + file.getFullPathName())
                                          .withButton ("OK"),
                                      nullptr);
                          });
}

void BeatBreakEditor::loadPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Load preset",
                                                   BeatBreakProcessor::getPresetDirectory(),
                                                   "*" + BeatBreakProcessor::getPresetExtension());

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (file == juce::File() || ! file.existsAsFile())
                                  return;

                              if (proc.loadPreset (file))
                              {
                                  timerCallback();
                                  repaint();
                              }
                              else
                              {
                                  juce::NativeMessageBox::showAsync (
                                      juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("BeatBreak")
                                          .withMessage (file.getFileName() + " is not a BeatBreak preset")
                                          .withButton ("OK"),
                                      nullptr);
                              }
                          });
}

void BeatBreakEditor::timerCallback()
{
    const auto t = proc.getActiveTimeSlot();
    const auto v = proc.getActiveVolumeSlot();

    timeSlots.setSelectedSlot (t);
    volumeSlots.setSelectedSlot (v);

    timeSlotName.setText (proc.getSlotName (true, t), juce::dontSendNotification);
    volSlotName.setText (proc.getSlotName (false, v), juce::dontSendNotification);

    // Cheap enough at 15 Hz, and it catches renames a preset load brought in.
    timeSlots.refreshNames();
    volumeSlots.refreshNames();

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

bool BeatBreakEditor::keyPressed (const juce::KeyPress& key)
{
    // Belt and braces for hosts that hand our window the focus anyway: pass
    // the transport key up to the host's own window instead of eating it.
   #if JUCE_WINDOWS
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        if (auto* peer = getPeer())
        {
            if (auto* handle = (HWND) peer->getNativeHandle())
            {
                if (auto* root = GetAncestor (handle, GA_ROOT))
                {
                    PostMessage (root, WM_KEYDOWN, (WPARAM) VK_SPACE, 0);
                    PostMessage (root, WM_KEYUP, (WPARAM) VK_SPACE, 0);
                    return true;
                }
            }
        }
    }
   #endif

    juce::ignoreUnused (key);
    return false;
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

    auto presets = header.removeFromRight (128).withTrimmedTop (12).withTrimmedBottom (8);
    presetLoad.setBounds (presets.removeFromRight (62).reduced (2, 0));
    presetSave.setBounds (presets.removeFromRight (62).reduced (2, 0));

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

        // Two knob columns: one tall column would push the knobs down to the
        // size of a dot, and it leaves the buttons a row wide enough to read.
        const auto knobColumns = knobs.size() > 1 ? 2 : 1;
        auto side = section.removeFromRight (96 * knobColumns);

        // One row of buttons at the bottom, whatever the section: stacking
        // them eats the height the knobs need to stay readable.
        const auto knobRows = ((int) knobs.size() + knobColumns - 1) / knobColumns;
        const auto buttonHeight = 24;
        const auto rowHeight = juce::jlimit (56, 92,
                                             (side.getHeight() - buttonHeight - 4)
                                                 / juce::jmax (1, knobRows) - 6);

        for (size_t i = 0; i < knobs.size(); i += (size_t) knobColumns)
        {
            auto row = side.removeFromTop (rowHeight);

            for (int c = 0; c < knobColumns; ++c)
            {
                auto cell = row.removeFromLeft (row.getWidth() / (knobColumns - c));

                if (i + (size_t) c < knobs.size())
                    knobs[i + (size_t) c]->setBounds (cell);
            }

            side.removeFromTop (6);
        }

        auto buttonRow = side.removeFromTop (buttonHeight);

        for (size_t i = 0; i < buttons.size(); ++i)
            buttons[i]->setBounds (buttonRow.removeFromLeft (buttonRow.getWidth() / (int) (buttons.size() - i))
                                       .reduced (2, 1));

        editor.setBounds (section.withTrimmedRight (8));
    };

    layoutSection (area.removeFromTop (halfHeight), timeEnable, timeSlotName, timeSlots, timeEditor,
                   { &timeAmount, &smoothing }, { &timeClear, &timeReverse, &timeFactory });

    layoutSection (area, volEnable, volSlotName, volumeSlots, volumeEditor,
                   { &volAmount, &volTension, &volAttack, &volRelease },
                   { &volClear, &volReverse, &volFactory });
}
