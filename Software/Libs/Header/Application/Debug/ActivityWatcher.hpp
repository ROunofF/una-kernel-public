/**
 ******************************************************************************
 * @file    ActivityWatcher.hpp
 * @brief   Debug-only mock that polls Activity::SensorPolicy in lieu of
 *          DailyHealth::Service.
 *
 *          DH normally reads IActivityDataSource on its minute tick
 *          to integrate counters into DailyMetrics. While DH is
 *          disabled (per the TEMP block in Backend), this watcher
 *          spins a low-priority thread that does the same getData()
 *          call and prints the snapshot, so we can observe exactly
 *          what Activity is producing.
 *
 *          Temporary - delete the file (and the init call in Backend)
 *          when DH is re-enabled.
 ******************************************************************************
 */

#ifndef __DEBUG_ACTIVITY_WATCHER_HPP
#define __DEBUG_ACTIVITY_WATCHER_HPP

#include "OS.hpp"

namespace Debug {

class ActivityWatcher
{
public:
    static ActivityWatcher& getInstance();

    bool init();
    bool deinit();

private:
    static constexpr uint32_t skPeriodMs = 10u * 1000u;

    ActivityWatcher() = default;
    ~ActivityWatcher() = default;

    void thread();

    OS::Thread<ActivityWatcher, 2048> mThread{};
};

} // namespace Debug

#endif
