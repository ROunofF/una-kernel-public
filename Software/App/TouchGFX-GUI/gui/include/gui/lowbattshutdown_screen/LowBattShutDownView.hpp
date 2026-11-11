#ifndef LOWBATTSHUTDOWNVIEW_HPP
#define LOWBATTSHUTDOWNVIEW_HPP

#include <gui_generated/lowbattshutdown_screen/LowBattShutDownViewBase.hpp>
#include <gui/lowbattshutdown_screen/LowBattShutDownPresenter.hpp>

class LowBattShutDownView : public LowBattShutDownViewBase
{
public:
    LowBattShutDownView();
    virtual ~LowBattShutDownView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

protected:
    uint16_t mCounter = Gui::Config::kLowBattShutdownScreenTimeout;
    bool mCommandSent {};
    virtual void handleTickEvent() override;
};

#endif // LOWBATTSHUTDOWNVIEW_HPP
