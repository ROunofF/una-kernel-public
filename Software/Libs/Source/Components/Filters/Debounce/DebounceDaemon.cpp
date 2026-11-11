/**
 ******************************************************************************
 * @file    DebounceDaemon.cpp
 * @brief   DebounceDaemon class.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "DebounceDaemon.hpp"

#define LOG_MODULE_PRX      "DebounceDaemon"
#define LOG_MODULE_LEVEL    LOG_LEVEL_ERROR
#include "Logger.h"

#include <cassert>

namespace Filter
{

bool DebounceDaemon::addObj(Debounce *pObj)
{
    assert(pObj != nullptr);

    for (uint32_t i = 0; i < skMaxFiltersNum; i++) {
        if (mpInstances[i] == nullptr) {
            mpInstances[i] = pObj;
            return true;
        }
    }

    LOG_ERROR("Failed to add filter\n");
    // No free space
    assert(false);

    return false;
}

void DebounceDaemon::delObj(Debounce *pObj)
{
    for (uint32_t i = 0; i < skMaxFiltersNum; i++) {
        if (mpInstances[i] == pObj) {
            if (mMutex.lock()) {
                mpInstances[i] = nullptr;
                mMutex.release();
            }
            return;
        }
    }
}

void DebounceDaemon::notify()
{
    mThread.flagsSet(skNotifyFlag);
}

DebounceDaemon::DebounceDaemon()
{
    mMutex.init(LOG_MODULE_PRX);
    mThread.init(LOG_MODULE_PRX, this, &DebounceDaemon::thread, osPriorityAboveNormal);
}

void DebounceDaemon::thread()
{

    for (;;) {

        uint8_t stillActive = 0;

        for (uint32_t i = 0; i < skMaxFiltersNum; i++) {
            if (mpInstances[i] != nullptr) {
                if (mMutex.lock()) {
                    stillActive |= mpInstances[i]->execute();
                    mMutex.release();
                }
            }
        }

        uint32_t waitTime = stillActive ? 10 : osWaitForever;

        mThread.flagsWait(skNotifyFlag, 0, waitTime);
    }

}

} /* namespace Filter */
