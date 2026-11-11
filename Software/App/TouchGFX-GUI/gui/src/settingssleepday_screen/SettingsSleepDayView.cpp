#include <gui/settingssleepday_screen/SettingsSleepDayView.hpp>
#include <gui/common/BacklightLabels.hpp>

namespace SleepMenu = Gui::Config::Menu::Top::Settings::Backlight::Sleep;

namespace
{

constexpr uint16_t kNoon = 12 * 60;

/// The "apply to" list, in SleepMenu::ApplyTo order.
constexpr TypedTextId kApplyLabels[SleepMenu::APPLY_COUNT] = {
    T_TEXT_THIS_DAY, T_TEXT_WEEK_DAYS, T_TEXT_WEEKENDS, T_TEXT_ALL_DAYS
};

} // namespace

SettingsSleepDayView::SettingsSleepDayView()
{

}

void SettingsSleepDayView::setupScreen()
{
    SettingsSleepDayViewBase::setupScreen();

    buttons.setL1(Buttons::WHITE);
    buttons.setL2(Buttons::WHITE);
    buttons.setR1(Buttons::AMBER);
    buttons.setR2(Buttons::WHITE);
}

void SettingsSleepDayView::tearDownScreen()
{
    SettingsSleepDayViewBase::tearDownScreen();
}

void SettingsSleepDayView::set(uint8_t day, uint16_t startMin, uint16_t endMin, bool twelveHour)
{
    mDay = day;
    mStartMin = startMin;
    mEndMin = endMin;
    m12Hour = twelveHour;

    Gui::Labels::toUpper(mTitle, kTitleSize, Gui::Labels::kDaysLong[day % WatchSettingsLocal::Sleep::kDays]);
    title.set(mTitle);

    timeMenu.set12Hour(twelveHour);

    mStep = START_HOURS;
    show(mStep);
}

SettingsSleepDayView::Step SettingsSleepDayView::next(Step step) const
{
    Step n = static_cast<Step>(step + 1);
    if (!m12Hour && (n == START_AMPM || n == END_AMPM)) {
        n = static_cast<Step>(n + 1);
    }
    return n;
}

SettingsSleepDayView::Step SettingsSleepDayView::prev(Step step) const
{
    Step p = static_cast<Step>(step - 1);
    if (!m12Hour && (p == START_AMPM || p == END_AMPM)) {
        p = static_cast<Step>(p - 1);
    }
    return p;
}

void SettingsSleepDayView::capture()
{
    if (mStep == APPLY_TO) {
        return;
    }

    uint16_t &minutes = bound(mStep);
    const bool pm = minutes >= kNoon;

    if (isTime(mStep)) {
        uint8_t h = 0;
        uint8_t m = 0;
        timeMenu.getTime(h, m);     // 0 .. 11 in 12-hour mode, 0 .. 23 otherwise
        if (m12Hour && pm) {
            h = static_cast<uint8_t>(h + 12);
        }
        minutes = static_cast<uint16_t>(h * 60 + m);
    } else {
        // AM/PM: the menu's second entry is PM.
        const bool nowPm = (menu.getSelectedItem() == 1);
        if (nowPm != pm) {
            minutes = nowPm ? static_cast<uint16_t>(minutes + kNoon)
                            : static_cast<uint16_t>(minutes - kNoon);
        }
    }
}

void SettingsSleepDayView::show(Step step)
{
    mStep = step;

    const bool time = isTime(step);

    // Time steps: the wheel, with this screen's own title, step name and
    // buttons. The others: the menu, which brings its own.
    timeMenu.setVisible(time);
    title.setVisible(time);
    messageText.setVisible(time);
    buttons.setVisible(time);
    menu.setVisible(!time);

    if (time) {
        const uint16_t minutes = bound(step);
        uint8_t h = static_cast<uint8_t>(minutes / 60);
        if (m12Hour) {
            h = static_cast<uint8_t>(h % 12);
        }
        timeMenu.setTime(h, static_cast<uint8_t>(minutes % 60));

        const bool hours = (step == START_HOURS || step == END_HOURS);
        if (hours) {
            timeMenu.setActiveHours();
        } else {
            timeMenu.setActiveMinutes();
        }

        TypedTextId label = T_TEXT_START_HOURS;
        if (step == START_MINUTES) {
            label = T_TEXT_START_MIN;
        } else if (step == END_HOURS) {
            label = T_TEXT_END_HOURS;
        } else if (step == END_MINUTES) {
            label = T_TEXT_END_MIN;
        }
        Unicode::snprintf(messageTextBuffer, MESSAGETEXT_SIZE, "%s", touchgfx::TypedText(label).getText());
    } else if (step == APPLY_TO) {
        menu.setTitle(T_TEXT_APPLY_TO_UC);
        menu.setNumberOfItems(SleepMenu::APPLY_COUNT);
        for (int16_t i = 0; i < SleepMenu::APPLY_COUNT; ++i) {
            menu.getSelectedItem(i)->config(kApplyLabels[i]);
            menu.getNotSelectedItem(i)->config(kApplyLabels[i]);
        }
        menu.selectItem(SleepMenu::APPLY_THIS_DAY);
    } else {
        menu.setTitle(step == START_AMPM ? T_TEXT_START_AM_PM : T_TEXT_END_AM_PM);
        menu.setNumberOfItems(2);
        menu.getSelectedItem(0)->config(T_TEXT_AM);
        menu.getNotSelectedItem(0)->config(T_TEXT_AM);
        menu.getSelectedItem(1)->config(T_TEXT_PM);
        menu.getNotSelectedItem(1)->config(T_TEXT_PM);
        menu.selectItem(bound(step) >= kNoon ? 1 : 0);
    }

    // Hide, then invalidate: invalidate() repaints the area either way.
    timeMenu.invalidate();
    title.invalidate();
    messageText.invalidate();
    buttons.invalidate();
    menu.invalidate();
}

void SettingsSleepDayView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        if (isTime(mStep)) {
            timeMenu.decValue();
        } else {
            menu.selectPrev();
        }
    }

    if (key == Gui::Config::Button::L2) {
        if (isTime(mStep)) {
            timeMenu.incValue();
        } else {
            menu.selectNext();
        }
    }

    if (key == Gui::Config::Button::R1) {
        if (mStep == APPLY_TO) {
            presenter->save(mDay, mStartMin, mEndMin,
                    static_cast<SleepMenu::ApplyTo>(menu.getSelectedItem()));
            application().gotoSettingsSleepScreenNoTransition();
        } else {
            capture();
            show(next(mStep));
        }
    }

    if (key == Gui::Config::Button::R2) {
        if (mStep == START_HOURS) {
            application().gotoSettingsSleepScreenNoTransition();
        } else {
            capture();
            show(prev(mStep));
        }
    }
}
