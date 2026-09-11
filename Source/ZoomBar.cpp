#include "ZoomBar.h"

namespace
{
    const juce::Colour barBackColour  { 0xff14161c };
    const juce::Colour trackColour    { 0xff1f2430 };
    const juce::Colour blockColour    { 0xff3a4354 };
    const juce::Colour edgeColour     { 0xff7d8ba6 };
    const juce::Colour tickColour     { 0x33ffffff };
    const juce::Colour playheadColour { 0xaaffffff };

    constexpr float arrowWidth = 14.0f;
    constexpr float edgeGrab = 5.0f;
}

ZoomBar::ZoomBar()
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

//==============================================================================

void ZoomBar::setRange (float newStart, float newEnd, juce::NotificationType notify)
{
    const auto oldStart = start;
    const auto oldEnd = end;

    applyRange (newStart, newEnd);

    if (notify == juce::sendNotification
        && onRangeChanged != nullptr
        && (! juce::approximatelyEqual (oldStart, start) || ! juce::approximatelyEqual (oldEnd, end)))
        onRangeChanged (start, end);
}

void ZoomBar::applyRange (float newStart, float newEnd)
{
    auto span = juce::jlimit (minimumSpan, 1.0f, newEnd - newStart);
    auto s = juce::jlimit (0.0f, 1.0f - span, newStart);

    start = s;
    end = s + span;
    repaint();
}

void ZoomBar::setBeatsPerLoop (int beats)
{
    beats = juce::jlimit (1, 64, beats);

    if (beats != beatsPerLoop)
    {
        beatsPerLoop = beats;
        repaint();
    }
}

void ZoomBar::setPlayhead (float phase)
{
    phase = juce::jlimit (0.0f, 1.0f, phase);

    // Only worth a repaint once it has moved a pixel.
    if (std::abs (phase - playhead) > 1.0f / juce::jmax (1.0f, (float) getWidth()))
    {
        playhead = phase;
        repaint();
    }
}

//==============================================================================

juce::Rectangle<float> ZoomBar::trackBounds() const
{
    return getLocalBounds().toFloat().reduced (arrowWidth + 2.0f, 2.0f);
}

juce::Rectangle<float> ZoomBar::blockBounds() const
{
    const auto track = trackBounds();

    return { track.getX() + start * track.getWidth(), track.getY(),
             juce::jmax (6.0f, (end - start) * track.getWidth()), track.getHeight() };
}

float ZoomBar::positionFor (float screenX) const
{
    const auto track = trackBounds();
    return juce::jlimit (0.0f, 1.0f, (screenX - track.getX()) / juce::jmax (1.0f, track.getWidth()));
}

ZoomBar::Zone ZoomBar::zoneAt (juce::Point<float> pos) const
{
    if (pos.x < arrowWidth)
        return Zone::leftArrow;

    if (pos.x > (float) getWidth() - arrowWidth)
        return Zone::rightArrow;

    const auto block = blockBounds();

    if (std::abs (pos.x - block.getX()) <= edgeGrab)
        return Zone::leftEdge;

    if (std::abs (pos.x - block.getRight()) <= edgeGrab)
        return Zone::rightEdge;

    if (block.contains (pos))
        return Zone::body;

    return Zone::track;
}

//==============================================================================

void ZoomBar::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (barBackColour);
    g.fillRoundedRectangle (bounds, 3.0f);

    const auto track = trackBounds();
    g.setColour (trackColour);
    g.fillRect (track);

    // ---- beat ticks ---------------------------------------------------------
    g.setColour (tickColour);

    for (int i = 1; i < beatsPerLoop; ++i)
    {
        const auto x = track.getX() + track.getWidth() * (float) i / (float) beatsPerLoop;
        g.drawVerticalLine ((int) std::round (x), track.getY() + 2.0f, track.getBottom() - 2.0f);
    }

    // ---- the visible window -------------------------------------------------
    const auto block = blockBounds();

    g.setColour (blockColour.withAlpha (dragZone != Zone::none ? 1.0f : 0.9f));
    g.fillRoundedRectangle (block, 2.0f);

    const auto edgeLit = [this] (Zone zone)
    {
        return dragZone == zone || (dragZone == Zone::none && hoverZone == zone);
    };

    g.setColour (edgeLit (Zone::leftEdge) ? juce::Colours::white : edgeColour);
    g.fillRect (block.getX(), block.getY(), 2.0f, block.getHeight());

    g.setColour (edgeLit (Zone::rightEdge) ? juce::Colours::white : edgeColour);
    g.fillRect (block.getRight() - 2.0f, block.getY(), 2.0f, block.getHeight());

    // ---- playhead -----------------------------------------------------------
    g.setColour (playheadColour);
    g.drawVerticalLine ((int) std::round (track.getX() + playhead * track.getWidth()),
                        track.getY(), track.getBottom());

    // ---- step arrows --------------------------------------------------------
    const auto arrow = [&g, &bounds] (bool pointsLeft, bool lit)
    {
        const auto centreX = pointsLeft ? arrowWidth * 0.5f : bounds.getWidth() - arrowWidth * 0.5f;
        const auto centreY = bounds.getCentreY();
        const auto w = 3.5f, h = 5.0f;

        juce::Path p;
        p.addTriangle (centreX + (pointsLeft ? -w : w), centreY,
                       centreX + (pointsLeft ? w : -w), centreY - h,
                       centreX + (pointsLeft ? w : -w), centreY + h);

        g.setColour (lit ? juce::Colours::white : edgeColour);
        g.fillPath (p);
    };

    arrow (true, hoverZone == Zone::leftArrow);
    arrow (false, hoverZone == Zone::rightArrow);
}

//==============================================================================

void ZoomBar::mouseMove (const juce::MouseEvent& e)
{
    const auto zone = zoneAt (e.position);

    if (zone != hoverZone)
    {
        hoverZone = zone;

        setMouseCursor (zone == Zone::leftEdge || zone == Zone::rightEdge
                            ? juce::MouseCursor::LeftRightResizeCursor
                            : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ZoomBar::mouseDown (const juce::MouseEvent& e)
{
    const auto zone = zoneAt (e.position);
    const auto span = end - start;

    switch (zone)
    {
        case Zone::leftArrow:   scrollBy (-span * 0.25f); return;
        case Zone::rightArrow:  scrollBy (span * 0.25f);  return;

        case Zone::track:
            // Clicking the empty track centres the window there, like Edison.
            setRange (positionFor (e.position.x) - span * 0.5f,
                      positionFor (e.position.x) + span * 0.5f, juce::sendNotification);
            dragZone = Zone::body;
            dragAnchor = 0.5f;
            return;

        case Zone::body:
            dragZone = Zone::body;
            dragAnchor = positionFor (e.position.x) - start;
            return;

        case Zone::leftEdge:
        case Zone::rightEdge:
            dragZone = zone;
            return;

        case Zone::none:
        default:
            return;
    }
}

void ZoomBar::mouseDrag (const juce::MouseEvent& e)
{
    if (dragZone == Zone::none)
        return;

    const auto position = positionFor (e.position.x);

    switch (dragZone)
    {
        case Zone::body:
            setRange (position - dragAnchor, position - dragAnchor + (end - start),
                      juce::sendNotification);
            break;

        case Zone::leftEdge:
            setRange (juce::jmin (position, end - minimumSpan), end, juce::sendNotification);
            break;

        case Zone::rightEdge:
            setRange (start, juce::jmax (position, start + minimumSpan), juce::sendNotification);
            break;

        case Zone::none:
        case Zone::leftArrow:
        case Zone::rightArrow:
        case Zone::track:
            break;
    }
}

void ZoomBar::mouseUp (const juce::MouseEvent&)
{
    dragZone = Zone::none;
    repaint();
}

void ZoomBar::mouseDoubleClick (const juce::MouseEvent&)
{
    setRange (0.0f, 1.0f, juce::sendNotification);
}

void ZoomBar::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto horizontal = std::abs (wheel.deltaX) > std::abs (wheel.deltaY);
    const auto step = horizontal ? wheel.deltaX : wheel.deltaY;

    if (std::abs (step) < 1.0e-4f)
        return;

    // Same deal as over the grids: shift (or a horizontal wheel) scrolls.
    if (e.mods.isShiftDown() || horizontal)
        scrollBy (-step * (end - start) * 0.25f);
    else
        zoomBy (step > 0.0f ? 1.0f / 1.25f : 1.25f, positionFor (e.position.x));
}

void ZoomBar::zoomBy (float factor, float around)
{
    const auto span = juce::jlimit (minimumSpan, 1.0f, (end - start) * factor);

    // Keep whatever is under the pointer where it is.
    const auto fraction = juce::jlimit (0.0f, 1.0f, (around - start) / juce::jmax (1.0e-6f, end - start));

    setRange (around - fraction * span, around + (1.0f - fraction) * span, juce::sendNotification);
}

void ZoomBar::scrollBy (float amount)
{
    setRange (start + amount, end + amount, juce::sendNotification);
}
