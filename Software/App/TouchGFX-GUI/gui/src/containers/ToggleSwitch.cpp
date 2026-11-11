#include <gui/containers/ToggleSwitch.hpp>

ToggleSwitch::ToggleSwitch()
{

}

void ToggleSwitch::initialize()
{
    ToggleSwitchBase::initialize();
}

void ToggleSwitch::setState(bool state)
{
    mState = state;
    railOn.setVisible(mState);
    if (mIsGray) { 
        railOff.setVisible(false);
        railOffGray.setVisible(!mState);
    } else {
        railOff.setVisible(!mState);
        railOffGray.setVisible(false);
    }
    
    if (mState) {
        handle.setX(30);
    } else { 
        handle.setX(0);
    }
    invalidate();
}

bool ToggleSwitch::getState() const
{
    return mState;
}

void ToggleSwitch::setBackgroundGray(bool setGray)
{
    mIsGray = setGray;
    setState(mState);
}