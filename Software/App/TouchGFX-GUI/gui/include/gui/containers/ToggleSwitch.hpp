#ifndef TOGGLESWITCH_HPP
#define TOGGLESWITCH_HPP

#include <gui_generated/containers/ToggleSwitchBase.hpp>

class ToggleSwitch : public ToggleSwitchBase
{
public:
    ToggleSwitch();
    virtual ~ToggleSwitch() {}

    virtual void initialize();

    void setState(bool state);
    bool getState() const;

    void setBackgroundGray(bool setGray);

protected:
    bool mState { };
    bool mIsGray { };
};

#endif // TOGGLESWITCH_HPP
