#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Edison-style horizontal zoom / scroll bar.

    The block shows which slice of the loop the grids are displaying: drag its
    middle to scroll, drag either edge to zoom, click the arrows to step, wheel
    to zoom around the pointer, double-click to fit the whole loop again.
*/
class ZoomBar final : public juce::Component
{
public:
    ZoomBar();

    /** Smallest slice of the loop that can be shown. */
    static constexpr float minimumSpan = 0.005f;

    void setRange (float newStart, float newEnd, juce::NotificationType notify);
    float getStart() const noexcept { return start; }
    float getEnd() const noexcept   { return end; }

    /** Ticks drawn in the track, so the bar reads as bars/beats. */
    void setBeatsPerLoop (int beats);
    void setPlayhead (float phase);

    std::function<void (float start, float end)> onRangeChanged;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Zone { none, leftArrow, rightArrow, leftEdge, rightEdge, body, track };

    juce::Rectangle<float> trackBounds() const;
    juce::Rectangle<float> blockBounds() const;
    Zone zoneAt (juce::Point<float> pos) const;
    float positionFor (float screenX) const;
    void applyRange (float newStart, float newEnd);
    void zoomBy (float factor, float around);
    void scrollBy (float amount);

    float start = 0.0f, end = 1.0f;
    int beatsPerLoop = 4;
    float playhead = 0.0f;

    Zone dragZone = Zone::none;
    float dragAnchor = 0.0f;     // where in the block the drag started
    Zone hoverZone = Zone::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoomBar)
};
