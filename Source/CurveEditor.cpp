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
    constexpr float handleRadius = 3.5f;
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

bool CurveEditor::hasTensionHandle (int segment) const
{
    const auto& points = curve().getPoints();

    return juce::isPositiveAndBelow (segment, (int) points.size() - 1)
             && points[(size_t) segment].shape == EnvelopeCurve::Shape::smooth;
}

juce::Point<float> CurveEditor::tensionHandlePosition (int segment) const
{
    const auto& points = curve().getPoints();
    const auto& a = points[(size_t) segment];
    const auto& b = points[(size_t) segment + 1];

    // Halfway along the segment, sitting on the curve it draws.
    const auto x = (a.x + b.x) * 0.5f;
    const auto y = a.y + (b.y - a.y) * EnvelopeCurve::shape (0.5f, a.tension, a.shape);

    return toScreen (x, y);
}

int CurveEditor::findTensionHandleNear (juce::Point<float> pos, float radiusPx) const
{
    const auto segments = curve().size() - 1;
    auto best = -1;
    auto bestDistance = radiusPx;

    for (int i = 0; i < segments; ++i)
    {
        if (! hasTensionHandle (i))
            continue;

        const auto d = tensionHandlePosition (i).getDistanceFrom (pos);

        if (d <= bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }

    return best;
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
    const auto& points = env.getPoints();
    juce::Path path;

    // Sampled per segment rather than per screen column: a point whose x falls
    // between two columns would otherwise be cut off by the polyline, which is
    // very visible once the tension gets steep.
    path.startNewSubPath (toScreen (points.front().x, points.front().y));

    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        const auto& a = points[i];
        const auto& b = points[i + 1];

        if (a.shape == EnvelopeCurve::Shape::step)
        {
            path.lineTo (toScreen (b.x, a.y));
            path.lineTo (toScreen (b.x, b.y));
            continue;
        }

        const auto widthPx = toScreen (b.x, 0.0f).x - toScreen (a.x, 0.0f).x;
        const auto steps = juce::jlimit (2, 512, (int) std::ceil (widthPx));

        for (int s = 1; s <= steps; ++s)
        {
            const auto u = (float) s / (float) steps;
            path.lineTo (toScreen (a.x + (b.x - a.x) * u,
                                   a.y + (b.y - a.y) * EnvelopeCurve::shape (u, a.tension, a.shape)));
        }
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

    // ---- tension handles on smooth segments ---------------------------------
    for (int i = 0; i + 1 < (int) points.size(); ++i)
    {
        if (! hasTensionHandle (i))
            continue;

        const auto p = tensionHandlePosition (i);
        const auto active = i == tensionSegment || i == hoverHandle;
        const auto radius = active ? handleRadius + 1.5f : handleRadius;

        // A diamond, so it does not read as another point.
        juce::Path diamond;
        diamond.addQuadrilateral (p.x, p.y - radius, p.x + radius, p.y,
                                  p.x, p.y + radius, p.x - radius, p.y);

        g.setColour (bgColour);
        g.fillPath (diamond);
        g.setColour (active ? juce::Colours::white : curveColour.withAlpha (0.9f));
        g.strokePath (diamond, juce::PathStrokeType (1.6f));
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
    const auto newHandle = newHover >= 0 ? -1 : findTensionHandleNear (e.position, grabRadius);

    if (newHover != hoverPoint || newHandle != hoverHandle)
    {
        hoverPoint = newHover;
        hoverHandle = newHandle;
        repaint();
    }
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto index = findPointNear (e.position, grabRadius);

    const auto handle = index >= 0 ? -1 : findTensionHandleNear (e.position, grabRadius);

    if (e.mods.isPopupMenu())
    {
        // On a point: its menu. On a tension handle: reset that tension.
        // Anywhere else: add a point there, or grab the one that already owns
        // that x column - never stack two in one column.
        if (index >= 0)
        {
            showPointMenu (index, e.getScreenPosition());
            return;
        }

        if (handle >= 0)
        {
            {
                const juce::SpinLock::ScopedLockType lock (proc.getCurveLock());
                curve().setTension (handle, 0.0f);
            }

            commit();
            return;
        }

        const auto norm = fromScreen (e.position);
        const auto fine = e.mods.isShiftDown();
        const auto x = snapX (norm.x, fine);
        const auto y = snapY (norm.y, fine);
        const auto tolerance = juce::jmax (EnvelopeCurve::minSpacing,
                                           grabRadius / juce::jmax (1.0f, plotBounds().getWidth()));

        {
            const juce::SpinLock::ScopedLockType lock (proc.getCurveLock());
            const auto existing = curve().findPointAtX (x, tolerance);

            draggedPoint = existing >= 0 ? curve().movePoint (existing, x, y)
                                         : curve().addPoint (x, y);
        }

        tensionSegment = -1;
        hoverPoint = draggedPoint;
        commit();
        return;
    }

    if (index >= 0)
    {
        draggedPoint = index;
        tensionSegment = -1;
        return;
    }

    // The handle bends its own segment; so does dragging the segment body.
    const auto segment = handle >= 0 ? handle : findSegmentNear (e.position);

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
    hoverHandle = -1;
    repaint();
}

void CurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Adding moved to the right button, so this only removes.
    const auto index = findPointNear (e.position, grabRadius);

    if (index < 0)
        return;

    {
        const juce::SpinLock::ScopedLockType lock (proc.getCurveLock());
        curve().removePoint (index);
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

void CurveEditor::showPointMenu (int index, juce::Point<int> screenPosition)
{
    juce::PopupMenu menu;
    const auto shape = curve().getPoints()[(size_t) index].shape;

    menu.addItem (1, "Linear / Curve", true, shape == EnvelopeCurve::Shape::curve);
    menu.addItem (2, "Step (hold)", true, shape == EnvelopeCurve::Shape::step);
    menu.addItem (3, "Smooth (S-curve)", true, shape == EnvelopeCurve::Shape::smooth);
    menu.addSeparator();
    menu.addItem (4, "Delete point", curve().size() > 2);

    // Target the click, not the component, or the menu lands at the middle
    // left of the editor.
    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
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
                case 4: curve().removePoint (index); break;
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
