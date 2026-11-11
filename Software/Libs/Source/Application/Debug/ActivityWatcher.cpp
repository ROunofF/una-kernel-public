/**
 ******************************************************************************
 * @file    ActivityWatcher.cpp
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ActWatch"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Debug/ActivityWatcher.hpp"
#include "Instances/InstanceActivityService.hpp"
#include "Activity/IActivityDataSource.hpp"

namespace Debug {

ActivityWatcher& ActivityWatcher::getInstance()
{
    static ActivityWatcher inst;
    return inst;
}

bool ActivityWatcher::init()
{
    return mThread.init("ActWatch", this, &ActivityWatcher::thread,
                        osPriorityLow, 5000);
}

bool ActivityWatcher::deinit()
{
    if (!mThread.isInitialized()) {
        return true;
    }
    mThread.deinit();
    return true;
}

void ActivityWatcher::thread()
{
    mThread.setInitialized();

    auto& src = Instance::ActivityService::getInstance().getDataSource();

    while (!mThread.shouldStop()) {
        uint32_t floorsUp   = 0;
        uint32_t floorsDown = 0;
        src.floorsSinceBoot(floorsUp, floorsDown);

        LOG_INFO("snapshot: steps=%u floorsUp=%u floorsDown=%u hr=%.1f activity=%u\n",
                 (unsigned)src.stepsSinceBoot(),
                 (unsigned)floorsUp,
                 (unsigned)floorsDown,
                 (double)src.currentAvgHr(),
                 (unsigned)src.activityMinutesSinceBoot());

        OS::Delay(skPeriodMs);
    }
}

} // namespace Debug
