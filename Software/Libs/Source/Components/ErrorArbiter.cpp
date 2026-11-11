/**
 ******************************************************************************
 * @file    ErrorArbiter.cpp
 ******************************************************************************
 */

#include "ErrorArbiter.hpp"

uint8_t ErrorArbiter::priorityOf(SystemError e)
{
    switch (e) {
    case SystemError::TEMPERATURE_SHUTDOWN: return 7;
    case SystemError::TEMPERATURE_CRITICAL: return 6;
    case SystemError::USB_PROTECTION:       return 5;
    case SystemError::TEMPERATURE_CABLE:    return 4;
    case SystemError::TEMPERATURE_AMBIENT:  return 3;
    case SystemError::SYS_VOLTAGE:          return 2;
    case SystemError::CABLE_CONNECTION:     return 1;
    default:                                return 0;   // NONE - never shown
    }
}

void ErrorArbiter::setActive(SystemError e)
{
    mActive.set(idx(e));
}

void ErrorArbiter::clearActive(SystemError e)
{
    mActive.reset(idx(e));

    // A full recovery starts the next episode from a clean slate.
    if (mActive.none()) {
        mAckLevel = 0;
    }
}

bool ErrorArbiter::isDismissable(SystemError e)
{
    switch (e) {
    case SystemError::TEMPERATURE_CRITICAL:   // "disconnect cable" - keep until resolved
    case SystemError::TEMPERATURE_SHUTDOWN:   // powering off
        return false;
    default:
        return true;
    }
}

void ErrorArbiter::acknowledge()
{
    const SystemError top = currentDisplay();
    if (top == SystemError::NONE || !isDismissable(top)) {
        return;
    }
    mAckLevel = priorityOf(top);
}

void ErrorArbiter::resetAcknowledge()
{
    mAckLevel = 0;
}

SystemError ErrorArbiter::highestActiveAbove(uint8_t level) const
{
    SystemError best     = SystemError::NONE;
    uint8_t     bestPrio = 0;

    for (size_t i = 0; i < mActive.size(); ++i) {
        if (!mActive.test(i)) {
            continue;
        }
        const SystemError e = static_cast<SystemError>(i);
        const uint8_t     p = priorityOf(e);
        if (p > level && p > bestPrio) {
            best     = e;
            bestPrio = p;
        }
    }

    return best;
}

SystemError ErrorArbiter::currentDisplay() const
{
    return highestActiveAbove(mAckLevel);
}

SystemError ErrorArbiter::currentCondition() const
{
    // Level 0 admits every priority, so acknowledgement plays no part here.
    return highestActiveAbove(0);
}
