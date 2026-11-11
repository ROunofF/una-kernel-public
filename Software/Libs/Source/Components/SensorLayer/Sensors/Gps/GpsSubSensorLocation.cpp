/**
 ******************************************************************************
 * @file    GpsSubSensorLocation.cpp
 * @brief   SubSensor for the GPS Location
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Gps.Sub.Loc"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Gps/GpsSubSensorLocation.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGpsLocation.hpp"
#include "System.hpp"

#include <algorithm>
#include <cmath>

namespace Sensor
{

namespace
{
// Plausibility gate thresholds. The goal is to reject gross outliers (a single
// corrupted coordinate lands hundreds of km away), not to filter real motion -
// the thresholds are intentionally generous so legitimate fast travel passes.
constexpr float   kMaxSpeedMps      = 100.0f;  // ~360 km/h ceiling on inter-fix speed
constexpr float   kMinJumpM         = 150.0f;  // always tolerate at least this between fixes
constexpr uint8_t kMaxRejectStreak  = 5;       // after this many rejects in a row, resync to the new position
constexpr float   kNullIslandDeg    = 0.001f;  // ~110 m around (0,0): a zeroed / collapsed fix

bool isPlausibleCoord(const Interface::IGps::LocationInfo& p)
{
    if (p.lat < -90.0f  || p.lat > 90.0f)  return false;
    if (p.lon < -180.0f || p.lon > 180.0f) return false;
    if (std::fabs(p.lat) < kNullIslandDeg && std::fabs(p.lon) < kNullIslandDeg) return false;
    return true;
}
} // namespace

GpsSubSensorLocation::GpsSubSensorLocation(Sensor::ISensorDriverCtrl& ctrl)
    : mDriver(*this,
              SDK::Sensor::Type::GPS_LOCATION,
              SDK::SensorDataParser::GpsLocation::getFieldsNumber(),
              ctrl)
    , mTick()
    , mFilterLat(0.5f)
    , mFilterLon(0.5f)
    , mFilterAlt(0.5f)
    , mPoint0()
    , mPoints()
    , mLastGood()
    , mLastGoodMs(0)
    , mRejectStreak(0)
    , mHasLastGood(false)
{
    mDriver.setSourceTick(&mTick);
    mPoints.init(LOG_MODULE_PRX);
}

Sensor::Driver& GpsSubSensorLocation::getDriver()
{
    return mDriver;
}

bool GpsSubSensorLocation::isActive()
{
    return mTick.isActive();
}

float GpsSubSensorLocation::start(float period)
{
    LOG_INFO("start GPS Location\n");

    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());

    mPoint0.valid = false;
    mPoints.clear();

    mHasLastGood  = false;
    mRejectStreak = 0;

    return period;
}

void GpsSubSensorLocation::stop()
{
    LOG_INFO("stop GPS Location\n");
    mTick.stop();

    // Drop the cached fix so a stopped sensor never serves a stale "valid"
    // location to the next consumer before fresh data arrives.
    mPoint0 = {};
    mPoints.clear();
    mFilterLat.reset();
    mFilterLon.reset();
    mFilterAlt.reset();

    mHasLastGood  = false;
    mRejectStreak = 0;
}

float GpsSubSensorLocation::updatePeriod(float period)
{
    mTick.start(period);
    mDriver.setSourceStepUs(mTick.stepUs());
    return period;
}

float GpsSubSensorLocation::getMinPeriod()
{
    return 1000.0f;
}

float GpsSubSensorLocation::getCurrentPeriod()
{
    return static_cast<float>(mTick.intervalMs());
}

const char* GpsSubSensorLocation::getDescription()
{
    return "Gps location";
}

void GpsSubSensorLocation::sensorRefresh()
{
    if (!mDriver.tickDue()) {
        return;
    }

    LOG_DEBUG("ENTRY\n");

    IGps::LocationInfo point;
    while (mPoints.pop(point, 0)) {
        if (!point.valid) {
            mPoint0.valid = false;
            continue;
        }

        if (!isPlausibleCoord(point)) {
            // A zeroed / out-of-range fix (e.g. a half-parsed coordinate that
            // collapsed toward null island). Drop it and hold the last good
            // position rather than feed garbage into the LPF.
            LOG_WARNING("drop implausible fix: lat %.6f lon %.6f\n", point.lat, point.lon);
            continue;
        }

        const uint32_t now    = ::Driver::System::GetTimeMs();
        bool           reseed = !mPoint0.valid;

        // Speed-gate against the last accepted fix. A single corrupted
        // coordinate appears as a jump of hundreds of km between consecutive
        // 1 Hz fixes - far above any real motion - so reject it before the LPF
        // can latch (or, on the re-seed path, force) the bad value.
        if (mHasLastGood) {
            const float dtSec   = (now - mLastGoodMs) / 1000.0f;
            const float maxJump = std::max(kMaxSpeedMps * dtSec, kMinJumpM);
            const float jump    = distanceFlatEarth(mLastGood, point.lat, point.lon);

            if (jump > maxJump) {
                if (mRejectStreak < kMaxRejectStreak) {
                    mRejectStreak++;
                    LOG_WARNING("reject outlier: jump %.0f m (max %.0f), "
                                "%.6f,%.6f -> %.6f,%.6f, dt %lu ms\n",
                                jump, maxJump,
                                mLastGood.lat, mLastGood.lon,
                                point.lat, point.lon,
                                static_cast<unsigned long>(now - mLastGoodMs));
                    continue;
                }
                // Persistent disagreement: the watch really did move far (a long
                // GPS gap, or a receiver reset). Resync to the new position
                // instead of staying stuck on the stale one.
                LOG_WARNING("resync after %u rejected fixes\n", mRejectStreak);
                reseed = true;
            }
        }

        mRejectStreak     = 0;
        mPoint0.precision = point.precision;

        if (reseed) {
            mPoint0.valid = true;
            mPoint0.lat   = mFilterLat.forceValue(point.lat);
            mPoint0.lon   = mFilterLon.forceValue(point.lon);
            mPoint0.alt   = mFilterAlt.forceValue(point.alt);
        } else {
            mFilterLat.execute(point.lat);
            mFilterLon.execute(point.lon);
            mFilterAlt.execute(point.alt);

            mPoint0.lat = mFilterLat.getValue();
            mPoint0.lon = mFilterLon.getValue();
            mPoint0.alt = mFilterAlt.getValue();
        }

        mLastGood    = point;
        mLastGoodMs  = now;
        mHasLastGood = true;
    }

    auto& sample = mDriver.getDataSample();
    sample.setTimestamp(::Driver::System::GetTimeMs());
    sample.f[SDK::SensorDataParser::GpsLocation::Field::PRECISION]    = mPoint0.precision;
    sample.u[SDK::SensorDataParser::GpsLocation::Field::COORDS_VALID] = mPoint0.valid ? 1UL : 0UL;
    sample.f[SDK::SensorDataParser::GpsLocation::Field::LAT]          = mPoint0.lat;
    sample.f[SDK::SensorDataParser::GpsLocation::Field::LON]          = mPoint0.lon;
    sample.f[SDK::SensorDataParser::GpsLocation::Field::ALT]          = mPoint0.alt;
    mDriver.pushDataSample();

    LOG_DEBUG("EXIT\n");
}

void GpsSubSensorLocation::setLocation(const Interface::IGps::LocationInfo& location)
{
    if (!mTick.isActive()) {
        return;
    }

    mPoints.push(location, 0);
}

} // namespace Sensor
