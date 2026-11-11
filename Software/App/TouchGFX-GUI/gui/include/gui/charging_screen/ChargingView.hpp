#ifndef CHARGINGVIEW_HPP
#define CHARGINGVIEW_HPP

#include <gui_generated/charging_screen/ChargingViewBase.hpp>
#include <gui/charging_screen/ChargingPresenter.hpp>

class ChargingView : public ChargingViewBase
{
public:
    ChargingView();
    virtual ~ChargingView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setBatteryLevel(uint8_t level);

protected:
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // CHARGINGVIEW_HPP
