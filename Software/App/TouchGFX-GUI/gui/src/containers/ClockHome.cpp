#include <gui/containers/ClockHome.hpp>

ClockHome::ClockHome()
{

}

void ClockHome::initialize()
{
    ClockHomeBase::initialize();
}

void ClockHome::setTime(uint8_t h, uint8_t m, uint8_t s)
{
    mHour = h;
    mMinute = m;
    mSecond = s;

    updateTime();
    // In the 12-hour format the date line carries the AM/PM suffix, so it
    // follows the hour.
    updateDate();
}

void ClockHome::setDate(uint8_t m, uint8_t d, uint8_t wd)
{
    mMonth = m;
    mDay = d;
    mWeekDay = wd;

    updateDate();
}

void ClockHome::setFormat(WatchSettingsLocal::Clock::Format format)
{
    if (mFormat == format) {
        return;
    }
    mFormat = format;

    updateTime();
    updateDate();
}

void ClockHome::setSeconds(bool state)
{
    // Seconds are not drawn yet: the setting is carried down to the container
    // so that the watch face has it once the layout for them exists.
    mSeconds = state;
}

void ClockHome::setDateOrder(WatchSettingsLocal::Clock::DateOrder order)
{
    if (mDateOrder == order) {
        return;
    }
    mDateOrder = order;

    updateDate();
}

void ClockHome::updateTime()
{
    if (mFormat == WatchSettingsLocal::Clock::FMT_12H) {
        uint8_t hour12 = mHour % 12;
        if (hour12 == 0) {
            hour12 = 12;
        }
        Unicode::snprintf(timeBuffer, TIME_SIZE, "%d:%02d", hour12, mMinute);
    } else {
        Unicode::snprintf(timeBuffer, TIME_SIZE, "%02d %02d", mHour, mMinute);
    }

    time.invalidate();
}

void ClockHome::updateDate()
{
    // Weekday always leads; the date-order setting swaps day and month, and the
    // 12-hour format appends the AM/PM marker to the date line.
    const touchgfx::Unicode::UnicodeChar* weekday =
        touchgfx::TypedText(Gui::Config::DateTime::DayShort[mWeekDay]).getText();
    const touchgfx::Unicode::UnicodeChar* month =
        touchgfx::TypedText(Gui::Config::DateTime::MonthShort[mMonth]).getText();
    const bool monthFirst = (mDateOrder == WatchSettingsLocal::Clock::DATE_MDY);

    if (mFormat == WatchSettingsLocal::Clock::FMT_12H) {
        const touchgfx::Unicode::UnicodeChar* ampm =
            touchgfx::TypedText(mHour < 12 ? T_TEXT_AM : T_TEXT_PM).getText();
        if (monthFirst) {
            Unicode::snprintf(dateBuffer, DATE_SIZE, "%s %s %d | %s", weekday, month, mDay, ampm);
        } else {
            Unicode::snprintf(dateBuffer, DATE_SIZE, "%s %d %s | %s", weekday, mDay, month, ampm);
        }
    } else {
        if (monthFirst) {
            Unicode::snprintf(dateBuffer, DATE_SIZE, "%s %s %d", weekday, month, mDay);
        } else {
            Unicode::snprintf(dateBuffer, DATE_SIZE, "%s %d %s", weekday, mDay, month);
        }
    }

    date.invalidate();
}
