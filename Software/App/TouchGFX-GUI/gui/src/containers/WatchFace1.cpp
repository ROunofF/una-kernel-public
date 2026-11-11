#include <gui/containers/WatchFace1.hpp>

WatchFace1::WatchFace1()
{

}

void WatchFace1::initialize()
{
    WatchFace1Base::initialize();

    activityBarTop.setVisible(true);
    activityBarTop.setType(ActivityBarTop::Type::STEP);
    activityBarTop.invalidate();

    activityBarBottom.setVisible(true);
    activityBarBottom.setType(ActivityBarBottom::Type::TIME);
    activityBarBottom.invalidate();
}

void WatchFace1::setTopBarProgress(uint32_t c, uint32_t t)
{
    // If total is zero, set progress to zero
    if (t == 0) {
        activityBarTop.setProgress(0);
        return;
    }
    uint32_t pct = c * 100 / t;
    if (pct > 100) {
        pct = 100;
    }
    activityBarTop.setProgress(static_cast<uint8_t>(pct));
}

void WatchFace1::setBotBarProgress(uint32_t c, uint32_t t)
{
    // The app widget owns the bottom bar while active; drop activity updates.
    if (mWidgetActive) {
        return;
    }

    // If total is zero, set progress to zero
    if (t == 0) {
        activityBarBottom.setProgress(0);
        return;
    }
    uint32_t pct = c * 100 / t;
    if (pct > 100) {
        pct = 100;
    }
    activityBarBottom.setProgress(static_cast<uint8_t>(pct));
}

void WatchFace1::setHomeWidget(const HomeWidgetData &widget)
{
    // Take over the bar only once the widget has something to show. A bare claim
    // (active with no fields -- e.g. an app's START before its first update) must
    // not switch the bar: it stays on daily health until content arrives, so a
    // claim alone never blanks it. mWidgetActive tracks "widget shown" (not merely
    // "claimed") because it also gates the daily bottom-bar updates.
    mWidgetActive = widget.active && (widget.showText || widget.showPercent);
    if (mWidgetActive) {
        // Widget mode: centre label instead of an icon, ring around it. float ->
        // the bar's uint8 here, at the render leaf.
        activityBarBottom.setType(ActivityBarBottom::Type::WIDGET);
        activityBarBottom.setMsg(widget.showText ? widget.text : "");
        activityBarBottom.setProgress(
            widget.showPercent ? static_cast<uint8_t>(widget.percent + 0.5f) : 0,
            widget.fromEnd);
    } else {
        // Default face. On a clear (STOP/death) the presenter repaints the daily
        // ring; on a claim-only START the ring was never overwritten and daily
        // updates keep flowing (mWidgetActive is false), so nothing to restore.
        activityBarBottom.setType(ActivityBarBottom::Type::TIME);
    }
    activityBarBottom.invalidate();
}

void WatchFace1::setMuteAlerts(bool muted)
{
    alertIcon.setVisible(muted);
    alertIcon.invalidate();
}

void WatchFace1::setTime(uint8_t h, uint8_t m, uint8_t s)
{
    clock.setTime(h, m, s);
}

void WatchFace1::setClockFormat(WatchSettingsLocal::Clock::Format format)
{
    clock.setFormat(format);
}

void WatchFace1::setClockSeconds(bool state)
{
    clock.setSeconds(state);
}

void WatchFace1::setClockDateOrder(WatchSettingsLocal::Clock::DateOrder order)
{
    clock.setDateOrder(order);
}

void WatchFace1::setDate(uint8_t m, uint8_t d, uint8_t wd)
{
    clock.setDate(m, d, wd);
}

void WatchFace1::setBatteryLevel(uint8_t level)
{
    battery.setBatteryLevel(level);
}
