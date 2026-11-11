/**
 ******************************************************************************
 * @file    InstanceDailyHealthService.hpp
 * @brief   Singleton holder for the Daily Health subsystem.
 *
 *          Owns the wall-clock source, the JSON/binary storage backend
 *          and the DailyHealth::Service worker.
 *
 *          Header-only: trivial glue with no out-of-line state, so
 *          there is no matching .cpp.
 ******************************************************************************
 */

#ifndef INSTANCE_DAILY_HEALTH_SERVICE_HPP
#define INSTANCE_DAILY_HEALTH_SERVICE_HPP

#include <ctime>

#include "DailyHealth/Service.hpp"
#include "DailyHealth/JsonFileStorage.hpp"
#include "Instances/InstanceActivityService.hpp"
#include "Rtc.hpp"
#include "main.h"

namespace Instance {

class DailyHealthService
{
public:
    static DailyHealthService& getInstance()
    {
        static DailyHealthService inst;
        return inst;
    }

    DailyHealth::Service& getService() { return mService; }

private:
    class SystemClock : public DailyHealth::IClock
    {
    public:
        std::tm now() const override
        {
            std::time_t t = std::time(nullptr);
            std::tm dt{};
            localtime_r(&t, &dt);

            return dt;
        }

        bool isValid() const override
        {
            // Both: GPS sets the time without a zone, and until the
            // phone sends one, local time is UTC and can be on another
            // date.
            Driver::Rtc& rtc = Driver::Rtc::GetInstance();
            return rtc.isTimeSet() && rtc.isTimeZoneSet();
        }
    };

    DailyHealthService()
        : mClock()
        , mStorage(DAILY_HEALTH_DIR_PATH, DAILY_HEALTH_TMP_FULL_PATH)
        , mService(mClock,
                   ActivityService::getInstance().getDataSource(),
                   mStorage)
    {
    }

    ~DailyHealthService() = default;

    SystemClock                  mClock;
    DailyHealth::JsonFileStorage mStorage;
    DailyHealth::Service         mService;
};

} // namespace Instance

#endif // INSTANCE_DAILY_HEALTH_SERVICE_HPP
