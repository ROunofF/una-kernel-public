#include <gui/settingssleep_screen/SettingsSleepView.hpp>
#include <gui/common/BacklightLabels.hpp>

namespace SleepMenu = Gui::Config::Menu::Top::Settings::Backlight::Sleep;

static_assert(SleepMenu::ID_LAST_DAY - SleepMenu::ID_FIRST_DAY + 1
                  == WatchSettingsLocal::Sleep::kDays,
              "One menu item per day");

SettingsSleepView::SettingsSleepView()
{

}

void SettingsSleepView::setupScreen()
{
    SettingsSleepViewBase::setupScreen();

    menu.setTitle(T_TEXT_SLEEP_MODE_UC);

    menu.setNumberOfItems(SleepMenu::ID_COUNT);

    MenuItemSelected *pS = menu.getSelectedItem(SleepMenu::ID_SCHEDULE);
    pS->configToggle(T_TEXT_SCHEDULE);
    pS->setMsgTypedTextId(T_TMP_SEMIBOLD_25);
    setSchedule(mSchedule);

    // The days are configured by setWindows(), which the presenter calls.
}

void SettingsSleepView::tearDownScreen()
{
    SettingsSleepViewBase::tearDownScreen();
}

void SettingsSleepView::setPositionId(uint16_t id)
{
    menu.selectItem(id);
}

uint16_t SettingsSleepView::getPositionId()
{
    return menu.getSelectedItem();
}

void SettingsSleepView::setSchedule(bool on)
{
    mSchedule = on;

    menu.getSelectedItem(SleepMenu::ID_SCHEDULE)->setToggle(on);

    MenuItemNotSelected *pN = menu.getNotSelectedItem(SleepMenu::ID_SCHEDULE);
    pN->configTip(T_TEXT_SCHEDULE, on ? T_TEXT_ON_UC : T_TEXT_OFF_UC);
    pN->setTipColor(on ? Gui::Config::Color::MENU_NOT_SELECTED_TIP_ON
                       : Gui::Config::Color::MENU_NOT_SELECTED_TIP_OFF);

    menu.invalidate();
}

void SettingsSleepView::setWindows(const WatchSettingsLocal::Sleep &sleep, bool twelveHour)
{
    for (uint8_t d = 0; d < Sleep::kDays; ++d) {
        Gui::Labels::formatWindow(mWindowText[d], kWindowTextSize,
                sleep.days[d].startMin, sleep.days[d].endMin, twelveHour);

        const int16_t id = static_cast<int16_t>(SleepMenu::ID_FIRST_DAY + d);
        menu.getSelectedItem(id)->configTip(Gui::Labels::kDaysShort[d], mWindowText[d]);
        menu.getNotSelectedItem(id)->configTip(Gui::Labels::kDaysShort[d], mWindowText[d]);
    }

    menu.invalidate();
}

void SettingsSleepView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        menu.selectPrev();
    }

    if (key == Gui::Config::Button::L2) {
        menu.selectNext();
    }

    if (key == Gui::Config::Button::R1) {
        const uint16_t id = menu.getSelectedItem();

        if (id == SleepMenu::ID_SCHEDULE) {
            setSchedule(!mSchedule);
            presenter->saveSchedule(mSchedule);
        } else if (id >= SleepMenu::ID_FIRST_DAY && id <= SleepMenu::ID_LAST_DAY) {
            presenter->editDay(static_cast<uint8_t>(id - SleepMenu::ID_FIRST_DAY));
            application().gotoSettingsSleepDayScreenNoTransition();
        }
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoSettingsBacklightScreenNoTransition();
    }
}
