/*
    State checks: preset files and host state must carry the parameters, all
    72 curves and any renamed slots. The curve text format is what breaks
    saved sessions when it changes, so this is the target that catches it.

    Build:  cmake --build build --target BeatBreakStateTests
    Run:    ./build/BeatBreakStateTests_artefacts/Release/BeatBreakStateTests
*/
#include "../Source/PluginProcessor.h"
#include <iostream>

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  pass  " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("bb-roundtrip.bbpreset");
    file.deleteFile();

    {
        BeatBreakProcessor p;
        p.setGridDivisions (96);
        p.setSnapEnabled (false);
        p.setSlotName (true, 5, "My Chop");
        p.setSlotName (false, 7, "My Gate");
        p.getTimeCurve (5).setToFlat (0.42f);
        p.apvts.getParameter ("volAttack")->setValueNotifyingHost (
            p.apvts.getParameter ("volAttack")->convertTo0to1 (123.0f));
        check (p.savePreset (file), "savePreset writes the file");
    }

    {
        BeatBreakProcessor p;
        check (p.getSlotName (true, 5) == FactoryPatterns::getTimeName (5), "fresh instance has factory names ("
                                                             + p.getSlotName (true, 5) + ")");
        check (p.getGridDivisions() == BeatBreakProcessor::defaultGridDivisions
                   && p.isSnapEnabled(), "fresh instance defaults to 1/16 with snap on");
        check (p.loadPreset (file), "loadPreset reads the file");
        check (p.getSlotName (true, 5) == "My Chop", "time slot name survives (" + p.getSlotName (true, 5) + ")");
        check (p.getSlotName (false, 7) == "My Gate", "volume slot name survives (" + p.getSlotName (false, 7) + ")");
        check (std::abs (p.getTimeCurve (5).getValue (0.5f) - 0.42f) < 1.0e-3f, "curve edit survives");
        check (p.getGridDivisions() == 96, "grid resolution survives a preset ("
                                               + juce::String (p.getGridDivisions()) + ")");
        check (! p.isSnapEnabled(), "snap setting survives a preset");
        const auto att = p.apvts.getRawParameterValue ("volAttack")->load();
        check (std::abs (att - 123.0f) < 0.5f, "parameter survives (" + juce::String (att) + ")");

        p.setSlotName (true, 5, {});
        check (p.getSlotName (true, 5) != "My Chop", "empty name restores the factory one");

        // Host state should carry the same things.
        juce::MemoryBlock block;
        p.setSlotName (false, 7, "Host Name");
        p.getStateInformation (block);
        BeatBreakProcessor q;
        q.setStateInformation (block.getData(), (int) block.getSize());
        check (q.getSlotName (false, 7) == "Host Name", "host state carries names");
        check (q.getGridDivisions() == 96 && ! q.isSnapEnabled(), "host state carries the grid settings");
    }

    check (! juce::File ("/nonexistent/nope.bbpreset").existsAsFile(), "sanity");
    {
        BeatBreakProcessor p;
        check (! p.loadPreset (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("not-a-preset.bbpreset")),
               "loading a missing file fails cleanly");
    }

    file.deleteFile();
    std::cout << (failures == 0 ? "all state checks passed" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
