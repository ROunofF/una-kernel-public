#include <gui/containers/ActivityBarBottom.hpp>

ActivityBarBottom::ActivityBarBottom()
{

}

void ActivityBarBottom::initialize()
{
    ActivityBarBottomBase::initialize();
}

void ActivityBarBottom::setType(Type type)
{
    iconTime.setVisible(false);
    iconMan.setVisible(false);
    iconStep.setVisible(false);
    msg.setVisible(false);

    switch (type) {
        case Type::TIME:
            iconTime.setVisible(true);
            break;
        case Type::RUNNING:
            iconMan.setVisible(true);
            break;
        case Type::STEP:
            iconStep.setVisible(true);
            break;
        case Type::WIDGET:
            msg.setVisible(true);
            break;
        default:
            break;
    }

    iconTime.invalidate();
    iconMan.invalidate();
    iconStep.invalidate();
    msg.invalidate();
}

void ActivityBarBottom::setMsg(const char *text)
{
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(text), msgBuffer, MSG_SIZE);
    msg.invalidate();
}

void ActivityBarBottom::setProgress(uint8_t pct, bool fromEnd)
{
    // Why this bar's arc math is the mirror of ActivityBarTop's
    // ---------------------------------------------------------------------
    // TouchGFX always sweeps an arc CLOCKWISE from its start angle. This bar's
    // background runs 100deg -> 260deg (lower-right -> bottom -> lower-left), so
    // a growing angle travels RIGHT-to-LEFT across the screen. The design wants
    // both the top and bottom bars to fill LEFT-to-RIGHT for a rising 0..100%,
    // so this bar is laid out mirrored -- and its arc endpoints therefore map to
    // the opposite visual sides from what their names suggest:
    //
    //     getArcStart() -> min (100deg) -> visual RIGHT -> the SDK's "end"
    //     getArcEnd()   -> max (260deg) -> visual LEFT  -> the SDK's "start"
    //
    // The home-widget contract is defined in those visual terms (start = left,
    // end = right) and never touches TouchGFX primitives, so it is the source of
    // truth; this method only adapts it onto the clockwise arc. Fed the same
    // 0..100%, the top and bottom bars fill the same visual direction despite
    // the opposite angle math.
    //
    // Range is read from the background arc, which we never mutate. Reading it
    // back off the progress arc would shrink the range each call, because
    // progress's own end moves in fromEnd mode.
    float min = 0;   // 100deg -- visual RIGHT / SDK "end"
    float max = 0;   // 260deg -- visual LEFT  / SDK "start"
    background.getArcStart(min);
    background.getArcEnd(max);

    const float filled = ((max - min) * pct) / 100.0f;
    if (fromEnd) {
        // Countdown: pin the fill to the SDK "end" (visual right = min); a
        // falling pct then drains the bar from the "start" (visual left = max).
        progress.setArc(min, min + filled);
    } else {
        // Default (daily activity): the fill grows from the SDK "start"
        // (visual left = max) toward the "end" as pct rises -- the same visual
        // left-to-right direction as the top bar.
        progress.setArc(max - filled, max);
    }

    progress.invalidate();
    background.invalidate();
}