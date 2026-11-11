#ifndef PHONENOTIFICATIONCONFIRMVIEW_HPP
#define PHONENOTIFICATIONCONFIRMVIEW_HPP

#include <gui_generated/phonenotificationconfirm_screen/PhoneNotificationConfirmViewBase.hpp>
#include <gui/phonenotificationconfirm_screen/PhoneNotificationConfirmPresenter.hpp>

class PhoneNotificationConfirmView : public PhoneNotificationConfirmViewBase
{
public:
    PhoneNotificationConfirmView();
    virtual ~PhoneNotificationConfirmView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:

    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // PHONENOTIFICATIONCONFIRMVIEW_HPP
