#include <gui/topmenufind_screen/TopMenuFindView.hpp>

TopMenuFindView::TopMenuFindView()
{

}

void TopMenuFindView::setupScreen()
{
    TopMenuFindViewBase::setupScreen();

    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::WHITE);
}

void TopMenuFindView::tearDownScreen()
{
    TopMenuFindViewBase::tearDownScreen();
}

void TopMenuFindView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {

    }

    if (key == Gui::Config::Button::L2) {

    }

    if (key == Gui::Config::Button::R1) {

    }

    if (key == Gui::Config::Button::R2) {
        application().gotoTopMenuScreenNoTransition();
    }
}