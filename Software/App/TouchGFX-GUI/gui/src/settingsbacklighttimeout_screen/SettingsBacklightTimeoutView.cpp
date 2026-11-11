#include <gui/settingsbacklighttimeout_screen/SettingsBacklightTimeoutView.hpp>
#include <gui/common/BacklightLabels.hpp>

using Backlight = WatchSettingsLocal::Backlight;

static_assert(sizeof(Gui::Labels::kBacklightTimeouts) / sizeof(Gui::Labels::kBacklightTimeouts[0])
                  == Backlight::kTimeoutCount,
              "One label per backlight timeout");

SettingsBacklightTimeoutView::SettingsBacklightTimeoutView()
{

}

void SettingsBacklightTimeoutView::setupScreen()
{
    SettingsBacklightTimeoutViewBase::setupScreen();

    menu.setTitle(T_TEXT_TIMEOUT_UC);

    menu.setNumberOfItems(Backlight::kTimeoutCount);

    for (int16_t i = 0; i < Backlight::kTimeoutCount; ++i) {
        menu.getSelectedItem(i)->config(Gui::Labels::kBacklightTimeouts[i]);
        menu.getNotSelectedItem(i)->config(Gui::Labels::kBacklightTimeouts[i]);
    }

    menu.invalidate();
}

void SettingsBacklightTimeoutView::tearDownScreen()
{
    SettingsBacklightTimeoutViewBase::tearDownScreen();
}

void SettingsBacklightTimeoutView::setTimeout(uint8_t seconds)
{
    for (int16_t i = 0; i < Backlight::kTimeoutCount; ++i) {
        if (Backlight::kTimeoutsS[i] == seconds) {
            menu.selectItem(i);
            return;
        }
    }
    menu.selectItem(0);
}

void SettingsBacklightTimeoutView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        menu.selectPrev();
    }

    if (key == Gui::Config::Button::L2) {
        menu.selectNext();
    }

    if (key == Gui::Config::Button::R1) {
        const uint16_t id = menu.getSelectedItem();
        if (id < Backlight::kTimeoutCount) {
            presenter->saveTimeout(Backlight::kTimeoutsS[id]);
        }
        application().gotoSettingsBacklightScreenNoTransition();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoSettingsBacklightScreenNoTransition();
    }
}
