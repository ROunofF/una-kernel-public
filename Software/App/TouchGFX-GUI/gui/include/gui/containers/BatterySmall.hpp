#ifndef BATTERYSMALL_HPP
#define BATTERYSMALL_HPP

#include <gui_generated/containers/BatterySmallBase.hpp>

class BatterySmall : public BatterySmallBase
{
public:
    BatterySmall();
    virtual ~BatterySmall() {}

    virtual void initialize();

    void setBatteryLevel(uint8_t level);
protected:
};

#endif // BATTERYSMALL_HPP
