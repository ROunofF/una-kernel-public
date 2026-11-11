#ifndef TURNOFFVIEW_HPP
#define TURNOFFVIEW_HPP

#include <gui_generated/turnoff_screen/TurnOffViewBase.hpp>
#include <gui/turnoff_screen/TurnOffPresenter.hpp>

class TurnOffView : public TurnOffViewBase
{
public:
    TurnOffView();
    virtual ~TurnOffView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

protected:
    virtual void handleKeyEvent(uint8_t key) override;

};

#endif // TURNOFFVIEW_HPP
