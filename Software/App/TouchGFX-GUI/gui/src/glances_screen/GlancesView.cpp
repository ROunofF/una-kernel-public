#include <gui/glances_screen/GlancesView.hpp>

GlancesView::GlancesView()
{

}

void GlancesView::setupScreen()
{
    GlancesViewBase::setupScreen();

    title.set(T_TEXT_GLANCES_UC);
}

void GlancesView::tearDownScreen()
{
    GlancesViewBase::tearDownScreen();
}

void GlancesView::setPositionId(uint16_t id)
{
    menu.selectItem(id);
}

uint16_t GlancesView::getPositionId()
{
    return menu.getSelectedItem();
}

void GlancesView::updAppList(const std::shared_ptr<std::vector<App::GlanceData>> &list)
{
    menu.updAppList(list);
}

void GlancesView::setNotifications(uint32_t callsNum, uint32_t emailsNum, uint32_t msgsNum)
{
    menu.setNotifications(callsNum, emailsNum, msgsNum);
}

void GlancesView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        mCounter = Gui::Config::kGlanceScreenTimeout;
        menu.selectPrev();
    }

    if (key == Gui::Config::Button::L2) {
        mCounter = Gui::Config::kGlanceScreenTimeout;
        menu.selectNext();
    }

    if (key == Gui::Config::Button::R1) {
        if (menu.isNotificationSelected()) {
            application().gotoPhoneNotificationListScreenNoTransition();
        }
        menu.selectNextLastActivity();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoHomeScreenNoTransition();
    }
}

void GlancesView::handleTickEvent()
{
    if (mCounter > 0) {
        mCounter--;
    }
    if (mCounter == 0) {
        application().gotoHomeScreenNoTransition();
    }

    if (mGlanceUpdCounter % GUI_CONFIG_MS_2_TICKS(1000) == 0) {
        presenter->requestGlanceList();
    }
    mGlanceUpdCounter++;
}
