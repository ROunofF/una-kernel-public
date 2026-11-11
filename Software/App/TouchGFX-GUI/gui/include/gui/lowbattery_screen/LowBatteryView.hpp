#ifndef LOWBATTERYVIEW_HPP
#define LOWBATTERYVIEW_HPP

#include <gui_generated/lowbattery_screen/LowBatteryViewBase.hpp>
#include <gui/lowbattery_screen/LowBatteryPresenter.hpp>

class LowBatteryView : public LowBatteryViewBase
{
public:
    LowBatteryView();
    virtual ~LowBatteryView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setBatteryLevel(uint8_t level);

protected:
    uint16_t mCounter = Gui::Config::kLowBattScreenTimeout;
    virtual void handleKeyEvent(uint8_t key) override;
    virtual void handleTickEvent() override;

};

#endif // LOWBATTERYVIEW_HPP
