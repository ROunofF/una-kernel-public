/**
 ******************************************************************************
 * @file    Backlight.cpp
 * @brief   Backlight component.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Backlight"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Backlight.hpp"

bool Backlight::init()
{
    if (mIsInit) {
        return true;
    }

    mTimer.init(LOG_MODULE_PRX, this, &Backlight::timCallback, false, nullptr);

    // Inverse logic.
    mIsInit = mPin.init(Interface::IGpo::Type::OD, true);

    if (mIsInit) {
        LOG_INFO("Initialized\n");
    } else {
        LOG_ERROR("Initialization failed\n");
    }

    return mIsInit;
}

bool Backlight::deinit()
{
    if (mIsInit) {
        mTimer.stop();
        mIsInit = !mPin.deinit();
    }

    return !mIsInit;
}

bool Backlight::on(uint32_t timeout)
{
    if (!mIsInit) {
        return false;
    }

    if (timeout > 0) {
        if(!mTimer.start(timeout)) {
            mPin.set(true); // fallback
            return false;
        }
    }

    LOG_DEBUG("timeout %d\n", timeout);

    return mPin.set(false); // Inverse logic
}

bool Backlight::off()
{
    if (!mIsInit) {
        return false;
    }

    mTimer.stop();

    LOG_DEBUG("\n");
    return mPin.set(true); // Inverse logic
}

bool Backlight::isOn()
{
    if (!mIsInit) {
        return false;
    }

    return !mPin.get(); // Inverse logic
}

void Backlight::timCallback(void *arg)
{
    LOG_DEBUG("off\n");
    mPin.set(true); // Inverse logic
}
