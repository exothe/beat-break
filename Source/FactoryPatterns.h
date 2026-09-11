#pragma once

#include "EnvelopeCurve.h"

/**
    The 36 time patterns and 36 volume patterns that fill the slot grid on a
    fresh instance, in the spirit of Gross Beat's factory slots.
*/
namespace FactoryPatterns
{
    static constexpr int numSlots = 36;

    juce::String getTimeName (int slot);
    juce::String getVolumeName (int slot);

    EnvelopeCurve getTimeCurve (int slot);
    EnvelopeCurve getVolumeCurve (int slot);
}
