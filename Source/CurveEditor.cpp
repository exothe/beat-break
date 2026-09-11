#include "CurveEditor.h"
#include "PluginProcessor.h"

namespace
{
    const juce::Colour bgColour        { 0xff14161c };
    const juce::Colour gridColour      { 0x22ffffff };
    const juce::Colour beatColour      { 0x44ffffff };
    const juce::Colour unityColour     { 0x33ff9e3d };
    const juce::Colour timeCurveColour { 0xff4ad0ff };
    const juce::Colour volCurveColour  { 0xffffc14a };
    const juce::Colour playheadColour  { 0xaaffffff };

    constexpr float pointRadius = 4.5f;
    constexpr float grabRadius = 9.0f;
}

CurveEditor::CurveEditor (BeatBreakProcessor& processor, Mode m)
    : proc (processor), mode (m)
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    startTimerHz (30);
}

CurveEditor::~CurveEditor()
{
    stopTimer();
}

//==============================================================================

EnvelopeCurve& CurveEditor::curve() const
{
    return mode == Mode::time ? proc.getTimeCurve (proc.getActiveTimeSlot())
                              : proc.getVolumeCurve (proc.getActiveVolumeSlot());
}

juce::Rectangle<float> CurveEditor::plotBounds() const
{
    return getLocalBounds().toFloat().reduced (1.0f);
}

juce::Point<float> CurveEditor::toScreen (float x, float y) const
{
    const auto r = plotBounds();
    return { r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight() };
}

juce::Point<float> CurveEditor::fromScreen (juce::Point<float> p) const
{
    const auto r = plotBounds();
    return { juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / juce::jmax (1.0f, r.getWidth())),
             juce::jlimit (0.0f, 1.0f, (r.getBottom() - p.y) / juce::jmax (1.0f, r.getHeight())) };
}

float CurveEditor::snapX (float x, bool fine) const
{
    if (fine || ! snapEnabled || gridDivisions <= 0)
        return x;

    const auto d = (float) gridDivisions;
    return juce::jlimit (0.0f, 1.0f, std::round (x * d) / d);
}

float CurveEditor::snapY (float y, bool fine) const
{
    if (fine || ! snapEnabled)
        return y;

    // The time grid snaps to the same divisions vertically, which is what
    // makes slice-aligned stutters easy to draw by hand.
    const auto d = mode == Mode::time ? (float) gridDivisions : 8.0f;
    return juce::jlimit (0.0f, 1.0f, std::round (y * d) / d);
}

int CurveEditor::findPointNear (juce::Point<float> pos, float radiusPx) const
{
    const auto& points = curve().getPoints();
    auto best = -1;
    auto bestDist = radiusPx;

    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto d = toScreen (points[i].x, points[i].y).getDistanceFrom (pos);
        if (d <= bestDist)
        {
            bestDist = d;
            best = (int) i;
        }
    }

    return best;
}

int CurveEditor::findSegmentNear (juce::Point<float> pos) const
{
    const auto& points = curve().getPoints();
    const auto norm = fromScreen (pos);

    for (size_t i = 0; i + 1 < points.size(); ++i)
        if (points[i].x <= norm.x && norm.x <= points[i + 1].x)
            return (int) i;

    return -1;
}

void CurveEditor::commit()
{
    proc.publishActiveCurves();
    repaint();
}

//==============================================================================

void CurveEditor::paint (juce::Graphics& g)
{
    const auto r = plotBounds();

    g.setColour (bgColour);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    // ---- grid --------------------------------------------------------------
    const auto divisions = juce::jmax (1, gridDivisions);
    const auto beatsPerLoop = juce::jmax (1, juce::roundToInt (proc.getLoopBeats()));

    for (int i = 0; i <= divisions; ++i)
    {
        const auto x = r.getX() + r.getWidth() * (float) i / (float) divisions;
        const auto onBeat = (i * beatsPerLoop) % divisions == 0;
        g.setColour (onBeat ? beatColour : gridColour);
        g.drawVerticalLine ((int) std::round (x), r.getY(), r.getBottom());
    }

    const auto hLines = mode == Mode::time ? divisions : 8;
    for (int i = 0; i <= hLines; ++i)
    {
        const auto y = r.getBottom() - r.getHeight() * (float) i / (float) hLines;
        g.setColour (i % 4 == 0 ? beatColour : gridColour);
        g.drawHorizontalLine ((int) std::round (y), r.getX(), r.getRight());
    }

    // ---- unity playback line (time only) -----------------------------------
    if (mode == Mode::time)
    {
        juce::Path unity;
        unity.startNewSubPath (toScreen (0.0f, proc.getUnityY (0.0f)));
        unity.lineTo (toScreen (1.0f, proc.getUnityY (1.0f)));
        g.setColour (unityColour);
        g.strokePath (unity, juce::PathStrokeType (1.5f));
    }

    // ---- the curve ----------------------------------------------------------
    const auto& env = curve();
    juce::Path path;
    const auto steps = juce::jmax (2, (int) r.getWidth());

    for (int i = 0; i <= steps; ++i)
    {
        const auto x = (float) i / (float) steps;
        const auto p = toScreen (x, env.getValue (x));

        if (i == 0)
            path.startNewSubPath (p);
        else
            path.lineTo (p);
    }

    const auto curveColour = mode == Mode::time ? timeCurveColour : volCurveColour;

    if (mode == Mode::volume)
    {
        auto fill = path;
        fill.lineTo (toScreen (1.0f, 0.0f));
        fill.lineTo (toScreen (0.0f, 0.0f));
        fill.closeSubPath();
        g.setColour (curveColour.withAlpha (0.18f));
        g.fillPath (fill);
    }

    g.setColour (curveColour);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    // ---- points -------------------------------------------------------------
    const auto& points = env.getPoints();
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto p = toScreen (points[i].x, points[i].y);
        const auto isActive = (int) i == draggedPoint || (int) i == hoverPoint;
        const auto radius = isActive ? pointRadius + 1.5f : pointRadius;

        g.setColour (points[i].shape == EnvelopeCurve::Shape::step ? juce::Colours::white
                                                                   : curveColour.brighter (0.4f));
        if (points[i].shape == EnvelopeCurve::Shape::step)
            g.fillRect (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p));
        else
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p));

        if (points[i].shape == EnvelopeCurve::Shape::smooth)
        {
            g.setColour (bgColour);
            g.fillEllipse (juce::Rectangle<float> (radius, radius).withCentre (p));
        }
    }

    // ---- playhead -----------------------------------------------------------
    const auto phase = proc.getLoopPhase();
    const auto phaseX = r.getX() + r.getWidth() * phase;
    g.setColour (playheadColour);
    g.drawVerticalLine ((int) std::round (phaseX), r.getY(), r.getBottom());

    const auto dotY = mode == Mode::time ? proc.getCurrentTimeY() : proc.getCurrentGain();
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f)
                       .withCentre (toScreen (phase, juce::jlimit (0.0f, 1.0f, dotY))));

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
}

//==============================================================================

void CurveEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto newHover = findPointNear (e.position, grabRadius);

    if (newHover != hoverPoint)
    {
        hoverPoint = newHover;
        repaint();
    }
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto index = findPointNear (e.position, grabRadius);

    if (e.mods.isPopupMenu())
    {
        if (index >= 0)
            showPointMenu (index);

        return;
    }

    if (index >= 0)
    {
        draggedPoint = index;
        tensionSegment = -1;
        return;
    }

    // Dragging the body of a segment bends it.
    const auto segment = findSegmentNear (e.position);
    if (segment >= 0)
    {
        draggedPoint = -1;
        tensionSegment = segment;
        tensionStartY = e.position.y;
        tensionStartValue = curve().getPoints()[(size_t) segment].tension;
    }
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto fine = e.mods.isShiftDown();

    if (draggedPoint >= 0)
    {
        const auto norm = fromScreen (e.position);

        {
            const juce::SpinLock::ScopedLockType lock (proc.getCurveLock());
            draggedPoint = curve().movePoint (draggedPoint,
                                              snapX (norm.x, fine),
                                              snapY (norm.y, fine));
        }

        commit();
    }
    else if (tensionSegment >= 0)
    {
        const auto delta = (tensionStartY - e.position.y) / juce::jmax (40.0f, plotBounds().getHeight() * 0.5f);

        {
            const juce::SpinLock::ScopedLockType lock (proc.getCurveLock());
            curve().setTension (tensionSegment, tensionStartValue + delta * 2.0f);
        }

        commit();
    }
}

void CurveEditor::mouseUp (const juce::MouseEvent&)
{
    draggedPoint = -1;
    tensionSegment = -1;
    repaint();
}

void CurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto index = findPointNear (e.position, grabRadius);
    const auto norm = fromScreen (e.position);
    const auto fine = e.mods.isShiftDown();

    {
        const juce::SpinLock::ScopedLockType lock (proc.getCurveLock());

        if (index >= 0)
            curve().removePoint (index);
        else
            curve().addPoint (snapX (norm.x, fine), snapY (norm.y, fine));
    }

    hoverPoint = -1;
    commit();
}

void CurveEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto segment = findSegmentNear (e.position);
    if (segment < 0)
        return;

    {
        const juce::SpinLock::ScopedLockType lock (proc.getCurveLock());
        const auto current = curve().getPoints()[(size_t) segment].tension;
        curve().setTension (segment, current + wheel.deltaY * 2.0f);
    }

    commit();
}

void CurveEditor::showPointMenu (int index)
{
    juce::PopupMenu menu;
    const auto shape = curve().getPoints()[(size_t) index].shape;

    menu.addItem (1, "Linear / Curve", true, shape == EnvelopeCurve::Shape::curve);
    menu.addItem (2, "Step (hold)", true, shape == EnvelopeCurve::Shape::step);
    menu.addItem (3, "Smooth (S-curve)", true, shape == EnvelopeCurve::Shape::smooth);
    menu.addSeparator();
    menu.addItem (4, "Reset segment tension");
    menu.addItem (5, "Delete point", curve().size() > 2);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, index] (int result)
    {
        if (result == 0)
            return;

        {
            const juce::SpinLock::ScopedLockType lock (proc.getCurveLock());

            switch (result)
            {
                case 1: curve().setShape (index, EnvelopeCurve::Shape::curve); break;
                case 2: curve().setShape (index, EnvelopeCurve::Shape::step); break;
                case 3: curve().setShape (index, EnvelopeCurve::Shape::smooth); break;
                case 4: curve().setTension (index, 0.0f); break;
                case 5: curve().removePoint (index); break;
                default: break;
            }
        }

        commit();
    });
}

//==============================================================================

void CurveEditor::setGridDivisions (int divisions)
{
    gridDivisions = juce::jlimit (1, 64, divisions);
    repaint();
}

void CurveEditor::clearCurve()
{
    {
        const juce::SpinLock::ScopedLockType lock (proc.getCurveLock());

        if (mode == Mode::time)
            curve().setToRamp();
        else
            curve().setToFlat (1.0f);
    }

    commit();
}

void CurveEditor::reverseCurve()
{
    {
        const juce::SpinLock::ScopedLockType lock (proc.getCurveLock());
        curve().reverse();
    }

    commit();
}

void CurveEditor::resetToFactory()
{
    proc.resetSlotToFactory (mode == Mode::time,
                             mode == Mode::time ? proc.getActiveTimeSlot()
                                                : proc.getActiveVolumeSlot());
    repaint();
}

void CurveEditor::setAllSegmentShapes (EnvelopeCurve::Shape shape)
{
    {
        const juce::SpinLock::ScopedLockType lock (proc.getCurveLock());
        curve().setAllShapes (shape);
    }

    commit();
}

void CurveEditor::timerCallback()
{
    const auto phase = proc.getLoopPhase();

    // Only repaint when the playhead has actually moved a pixel or so.
    if (std::abs (phase - lastPhase) > 1.0f / juce::jmax (1.0f, (float) getWidth()))
    {
        lastPhase = phase;
        repaint();
    }
}
