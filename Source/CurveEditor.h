#pragma once

#include "EnvelopeCurve.h"
#include <juce_gui_basics/juce_gui_basics.h>

class BeatBreakProcessor;

/**
    The editable mapping grid.

    Time mode draws the unity-playback diagonal so it is obvious where a
    segment plays forwards at normal speed (parallel to it), backwards
    (falling), frozen (flat) or pitched (any other slope).
*/
class CurveEditor final : public juce::Component,
                          private juce::Timer,
                          private juce::KeyListener
{
public:
    enum class Mode { time, volume };

    CurveEditor (BeatBreakProcessor& processor, Mode mode);
    ~CurveEditor() override;

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;

    void setGridDivisions (int divisions);
    int getGridDivisions() const noexcept { return gridDivisions; }

    /** Slice of the loop the grid shows, 0..1. Both editors are kept on the
        same range so the time and volume grids stay lined up. */
    void setViewRange (float start, float end);
    void setSnapEnabled (bool shouldSnap) { snapEnabled = shouldSnap; }

    /** Buttons in the editor act on whichever slot is currently selected. */
    void clearCurve();
    void reverseCurve();
    void resetToFactory();
    void setAllSegmentShapes (EnvelopeCurve::Shape shape);

private:
    void timerCallback() override;

    EnvelopeCurve& curve() const;
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> fromScreen (juce::Point<float> p) const;
    float snapX (float x, bool fine) const;
    float snapY (float y, bool fine) const;
    int findPointNear (juce::Point<float> pos, float radiusPx) const;
    int findSegmentNear (juce::Point<float> pos) const;

    /** Smooth segments carry a tension handle halfway along them. */
    bool hasTensionHandle (int segment) const;
    juce::Point<float> tensionHandlePosition (int segment) const;
    int findTensionHandleNear (juce::Point<float> pos, float radiusPx) const;
    void showPointMenu (int index, juce::Point<int> screenPosition);

    /** The point menu's D shortcut: JUCE menus only handle the arrows, return
        and escape themselves, so we listen on the menu window. */
    using juce::Component::keyPressed;   // the KeyListener overload hides it
    bool keyPressed (const juce::KeyPress& key, juce::Component* originating) override;

    /** Drops the shortcut listener only. Releasing the borrowed focus here as
        well would dismiss the menu the listener was just attached to. */
    void detachMenuListener();

    /** Menu is over: drop the listener and hand the keyboard back. */
    void finishMenu();

    /** The editor holds no keyboard focus so the host keeps its transport
        keys, but a menu shortcut needs the key events to arrive at all. */
    void takeKeyboardFocusForMenu();
    void returnKeyboardFocusToHost();
    void commit();
    juce::Rectangle<float> plotBounds() const;

    BeatBreakProcessor& proc;
    const Mode mode;

    int gridDivisions = 16;
    bool snapEnabled = true;

    float viewStart = 0.0f, viewEnd = 1.0f;
    float viewSpan() const noexcept { return juce::jmax (1.0e-4f, viewEnd - viewStart); }

    int draggedPoint = -1;
    int tensionSegment = -1;
    float tensionStartY = 0.0f;
    float tensionStartValue = 0.0f;
    int hoverPoint = -1;
    int hoverHandle = -1;

    int menuPointIndex = -1;
    juce::Component::SafePointer<juce::Component> menuWindow;

    float lastPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveEditor)
};
