/**
 ******************************************************************************
 * @file    GpsAiroha.cpp
 * @brief   GPS component.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "GpsAiroha.hpp"

#define LOG_MODULE_PRX      "Gps.Airoha"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Rtc.hpp"
#include "FileSystem.hpp"
#include "SettingsLocalManager.hpp"
#include "SettingsDebugManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometerRaw.hpp"
#include "SensorLayer/Core/SensorManager.hpp"

#include <cassert>
#include <cstring>
#include <string_view>

#include "Instances/InstanceI2C.hpp"
#include "Gpio.hpp"

namespace Gps
{

GpsAiroha::GpsAiroha(Interface::IUart &uart, Interface::IGpo &pwr)
{
    config(uart, pwr);
}

void GpsAiroha::config(Interface::IUart &uart, Interface::IGpo &pwr)
{
    mDrv.config(uart, pwr);
    mAiding.config(Driver::Rtc::GetInstance(), FS::FileSystem::GetInstance(), GPS_EPO_FILE_DIR_PATH);
}

void GpsAiroha::nmeaLogToFileEnable(bool state)
{
    mLogToFileEnable = state;
}

bool GpsAiroha::init()
{
    bool status = mDrv.init();
    LOG_INFO("%s\n", status ? "OK" : "FAIL");

    (void)status;

    return status;
}

bool GpsAiroha::deinit()
{
    bool status = mDrv.deinit();
    LOG_INFO("%s\n", status ? "OK" : "FAIL");

    resetState();

    return true;
}

bool GpsAiroha::enable()
{
    LOG_INFO("Enabling...\n");
    if (!mDrv.isInitialized()) {
        LOG_ERROR("Invalid state!\n");
        return false;
    }

    Settings::LocalManager &slm = Settings::LocalManager::GetInstance();
    Settings::DebugManager &sdm = Settings::DebugManager::GetInstance();

    mLogToFileEnable = sdm.getSettings().gpsCfg.nmeaLog;
//    mBinLogToFileEnable = static_cast<Airoha::DebugLog>(sdm.getSettings().gpsCfg.binLog) != Airoha::DebugLog::DISABLE;
//    mImuLogToFileEnable = sdm.getSettings().gpsCfg.imuLog;

    mDrv.setRawDataCallback(this);
    mDrv.setNmeaCallback(this);
    mDrv.setAidingCallback(&mAiding);

    resetState();

    if (mLogToFileEnable || mBinLogToFileEnable|| mImuLogToFileEnable) {
        // Create name
        std::tm t {};
        std::time_t utc = time(nullptr);
        localtime_r(&utc, &t); // Takes into account timezone and DST
        char buff[FS::FileSystem::skMaxPathLen];

        if (mLogToFileEnable) {
            snprintf(buff, sizeof(buff), "%s%04u%02u%02u%02u%02u%02u.txt",
                            GPS_NMEA_LOG_DIR_PATH,
                            t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                            t.tm_hour, t.tm_min, t.tm_sec);

            mLogToFile.start(buff);
        }

//        if (mBinLogToFileEnable) {
//            snprintf(buff, sizeof(buff), "%s%04u%02u%02u%02u%02u%02u.bin",
//                            GPS_NMEA_LOG_DIR_PATH,
//                            t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
//                            t.tm_hour, t.tm_min, t.tm_sec);
//
//            mBinLogToFile.start(buff);
//        }

//        if (mImuLogToFileEnable) {
//            snprintf(buff, sizeof(buff), "%s%04u%02u%02u%02u%02u%02u_imu.csv",
//                            GPS_NMEA_LOG_DIR_PATH,
//                            t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
//                            t.tm_hour, t.tm_min, t.tm_sec);
//
//            mImuLogToFile.start(buff);
//            //mImu = new (std::nothrow) ImuBosch(Instance::I2C::getInstance().getSensor2Bus(),
//            //                    Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSOR2_INT2));
//            if (mImu) {
//                mImu->init();
//                mImu->setSensorCallback(this);
//                mImu->setFifoPeriod(1000);
//
//                const uint16_t odr = 2000;
//                mImu->setAccelRange(4);
//                mImu->setGyroRange(500);
//                mImu->enableAccel(odr);
//                mImu->enableGyro(odr);
//            }
//        }
    }

    uint32_t timeout = 0;
    if (mDrv.powerOn(timeout)) {
        mL1Mode = true;
        (void)slm;

        mDrv.lockSleep(timeout);    // PAIR382,1
        mDrv.softPowerOff(timeout); // PAIR003
        mDrv.enableDualBandMode(!mL1Mode); // PAIR104,x
        mDrv.softPowerOn(timeout);  // PAIR002

        if (!mL1Mode) {
            mDrv.unlockSleep(timeout);  // PAIR382,0
        } else {
            LOG_WARNING("Sleep disabled due L1 mode\n");
        }

        mDrv.start(Airoha::StartType::HOT, timeout); // PAIR004
        LOG_INFO("Enabled\n");
        return true;
    }

    // Fallback
    mLogToFile.stop();
//    mBinLogToFile.stop();
//    mImuLogToFile.stop();
//    if (mImu) {
//        mImu->disableAll();
//        mImu->setSensorCallback(nullptr);
//        mImu->deinit();
//        delete mImu;
//        mImu = nullptr;
//    }
    mDrv.powerOff(timeout);

    LOG_ERROR("Failed to enabled GPS\n");

    return false;
}

bool GpsAiroha::disable()
{
    LOG_INFO("Disabling...\n");
    if (!mDrv.isInitialized()) {
        LOG_ERROR("Invalid state!\n");
        return false;
    }

    mDrv.powerOff();
    mLogToFile.stop();
//    mBinLogToFile.stop();

//    mImuLogToFile.stop();
//    if (mImu) {
//        mImu->disableAll();
//        mImu->setSensorCallback(nullptr);
//        mImu->deinit();
//        delete mImu;
//        mImu = nullptr;
//    }
    resetState();

    mDrv.setRawDataCallback(nullptr);
    mDrv.setNmeaCallback(nullptr);
    mDrv.setAidingCallback(nullptr);

    LOG_INFO("Disabled\n");

    return true;
}

bool GpsAiroha::isEnabled()
{
    return mDrv.isPowerOn();
}

bool GpsAiroha::hasFix()
{
    return mLocation.valid;
}

void GpsAiroha::setPeriod(uint32_t seconds)
{

}

time_t GpsAiroha::getTime()
{
    return mTime;
}

float GpsAiroha::getSpeed()
{
    return mSpeed;
}

float GpsAiroha::getDistance()
{
    if (mL1Mode) {
        uint32_t time  = OS::GetTimeMs() - mDistanceGetTimestamp;
        if (time < 20000) {
            LOG_DEBUG("Distance backup:   %.2f m\n", mDistance);
            return mDistance;
        }
        mDistanceGetTimestamp = OS::GetTimeMs();
    }

    float distance = 0.0f;

    if (!isEnabled()) {
        return false;
    }
    bool isOn = false;
    if (mDrv.getOdometerStatus(isOn, distance)) {
        if (!isOn) {
            LOG_DEBUG("Odometer:   disabled\n");
            mDrv.enableOdometer(true);
        } else {
#if 1
        LOG_DEBUG("Distance:   %.2f m\n", distance);
        mDistance = distance;
#endif
        }
    }

    return mDistance;
}

Interface::IGps::LocationInfo GpsAiroha::getLocation()
{
    return mLocation;
}

Interface::IGps::EpoPeriod GpsAiroha::getEpoFilePeriod()
{
    Airoha::Aiding::EpoPeriod aiding = mAiding.getEpoTimeRange();
    Interface::IGps::EpoPeriod igps {};

    igps.gps.start = aiding.gps.start;
    igps.gps.end = aiding.gps.end;
    igps.glonass.start = aiding.glonass.start;
    igps.glonass.end = aiding.glonass.end;
    igps.galileo.start = aiding.galileo.start;
    igps.galileo.end = aiding.galileo.end;
    igps.beidou.start = aiding.beidou.start;
    igps.beidou.end = aiding.beidou.end;
    igps.qzss.start = aiding.qzss.start;
    igps.qzss.end = aiding.qzss.end;
    return igps;
}

void GpsAiroha::setReferenceLocation(float lat, float lon, float alt)
{
    mAiding.setReferenceLocation(lat, lon, alt);
}

bool GpsAiroha::getReferenceLocation(float& lat, float& lon, float& alt)
{
    return mAiding.getReferenceLocation(lat, lon, alt);
}

void GpsAiroha::resetState()
{
    mTime = 0;

    mLocation.valid = false;

    mLocation.lat = 0.0f;
    mLocation.lon = 0.0f;
    mLocation.alt = 0.0f;
    mLocation.precision = 0.0f;

    mSpeed = 0.0f;
    mDistance = 0.0f;
    mFixMode = Interface::IGps::FixMode::UNKNOWN;
}

namespace
{

// A GGA fix is "real" for any positioning mode that yields a measured
// position: standard GPS, differential, PPS, and RTK (fixed/float). INVALID,
// ESTIMATED (dead reckoning), MANUAL_INPUT and SIMULATION are not real fixes,
// so hasFix() stays false for them - consistent with the RMC fix-mode API
// (dead-reckoned speed is unreliable and must be rejected).
bool isRealGgaFix(Airoha::Nmea::GGA::FixQuality quality)
{
    switch (quality) {
    case Airoha::Nmea::GGA::GPS_FIX:
    case Airoha::Nmea::GGA::DGPS_FIX:
    case Airoha::Nmea::GGA::PPS_FIX:
    case Airoha::Nmea::GGA::RTK_FIX:
    case Airoha::Nmea::GGA::RTK_FLOAT:
        return true;
    default:
        return false;
    }
}

Interface::IGps::FixMode mapRmcMode(Airoha::Nmea::RMC::Mode mode)
{
    switch (mode) {
    case Airoha::Nmea::RMC::AUTONOMOUS:   return Interface::IGps::FixMode::AUTONOMOUS;
    case Airoha::Nmea::RMC::DIFFERENTIAL: return Interface::IGps::FixMode::DIFFERENTIAL;
    case Airoha::Nmea::RMC::PRECISE:      return Interface::IGps::FixMode::PRECISE;
    case Airoha::Nmea::RMC::ESTIMATED:    return Interface::IGps::FixMode::ESTIMATED;
    case Airoha::Nmea::RMC::MANUAL:       return Interface::IGps::FixMode::MANUAL;
    case Airoha::Nmea::RMC::SIMULATED:    return Interface::IGps::FixMode::SIMULATED;
    case Airoha::Nmea::RMC::NOT_VALID:    return Interface::IGps::FixMode::NOT_VALID;
    default:                              return Interface::IGps::FixMode::UNKNOWN;
    }
}

} // namespace

Interface::IGps::FixMode GpsAiroha::getFixMode()
{
    return mFixMode;
}

void GpsAiroha::onRawDataBin(uint8_t* data, size_t size)
{
#if 0
    LOG_DEBUG_DUMP(data, size);
#endif
    //mBinLogToFile.write(data, size);
}

void GpsAiroha::onRawDataRx(std::string_view rx)
{
#if 1
    LOG_DEBUG("[%.*s]\n", rx.size() - 2, rx.data()); // skip "\r\n" for pretty output
#endif
    mLogToFile.write(rx);
}

void GpsAiroha::onRawTokensRx(std::string_view *tokens, size_t count)
{
#if 0

#endif
}

void GpsAiroha::onRawDataTx(std::string_view tx)
{
#if 1
    LOG_DEBUG("[%.*s]\n", tx.size() - 2, tx.data()); // skip "\r\n" for pretty output
#endif
    mLogToFile.write(tx);
}

void GpsAiroha::onNmeaGGA(const Airoha::Nmea::GGA &cmd)
{
#if 1
    LOG_DEBUG("GGA: Fix    %u, Lat %.6f, Lon %.6f, Alt %.1f m, HDOP %.1f, Sat: %d\n",
            cmd.fix_quality, cmd.latitude, cmd.longitude, cmd.altitude, cmd.hdop, cmd.num_satellites);
#endif

    bool prev = mLocation.valid;

    mLocation.valid = isRealGgaFix(cmd.fix_quality);
    mLocation.lat = cmd.latitude;
    mLocation.lon = cmd.longitude;
    mLocation.alt = cmd.altitude;
    mLocation.precision = cmd.hdop;

    if (prev != mLocation.valid) {
        // Notify
        mObserver.notify(&Interface::IGps::Callback::onGpsFix, mLocation.valid);
    }

    mObserver.notify(&Interface::IGps::Callback::onGpsLocation, mLocation);
}

void GpsAiroha::onNmeaRMC(const Airoha::Nmea::RMC &cmd)
{
    // Convert to UTC seconds
    uint32_t utc_sec = Airoha::Nmea::NmeaToUtcSeconds(cmd.utc_time, cmd.date);

    // Convert to std::tm
    std::tm utc_tm {};
    Airoha::Nmea::NmeaToTm(cmd.utc_time, cmd.date, &utc_tm);

    // Convert to m/s
    float speed = Airoha::Nmea::KnotsToMps(cmd.speed_knots);

#if 1
    LOG_DEBUG("RMC: Status %u, UTC %u (%04u-%02u-%02u %02u:%02u:%02u), Speed %.1f m/s, Course %.1f degrees\n",
            cmd.status, utc_sec,
            utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday,
            utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec,
            speed, cmd.track_angle);
#endif

    if (cmd.status == Airoha::Nmea::RMC::VALID) {
        // Surface the receiver's fix mode so consumers can reject dead-reckoning
        // (ESTIMATED) fixes, whose extrapolated speed is unreliable.
        mFixMode = mapRmcMode(cmd.mode);
        mTime = utc_sec;
        mSpeed = speed;
        mObserver.notify(&Interface::IGps::Callback::onGpsTime, mTime);
    } else {
        // Void RMC: speed/time are invalid, so the fix mode must not advertise a
        // real fix - otherwise SPEED_VALID could latch true on a zeroed speed
        // even when the receiver reports the data is not valid.
        mFixMode = Interface::IGps::FixMode::NOT_VALID;
        mTime = 0;
        mSpeed = 0;
    }
}

void GpsAiroha::onNmeaGSV(const Airoha::Nmea::GSV &cmd)
{
#if 1
    const size_t buffSize = 256;
    char buff[buffSize];
    size_t pos = 0;

    // Skip empty strings
    if (cmd.num_satellites == 0) {
        return;
    }

    switch (cmd.system) {
        case Airoha::Nmea::GnssSystem::GPS:
            pos += snprintf(buff + pos, buffSize - pos, "GSV [GPS    ]: ");
            break;
        case Airoha::Nmea::GnssSystem::GLONASS:
            pos += snprintf(buff + pos, buffSize - pos, "GSV [GLONASS]: ");
            break;
        case Airoha::Nmea::GnssSystem::GALILEO:
            pos += snprintf(buff + pos, buffSize - pos, "GSV [GALILEO]: ");
            break;
        case Airoha::Nmea::GnssSystem::BEIDOU:
            pos += snprintf(buff + pos, buffSize - pos, "GSV [BEIDOU ]: ");
            break;
        default:
            pos += snprintf(buff + pos, buffSize - pos, "GSV [UNKNOWN]: ");
            break;
    }

    // Access satellite data regardless of GNSS system
    for (uint8_t i = 0; i < cmd.num_satellites; i++) {
        pos += snprintf(buff + pos, buffSize - pos, "PRN=%2d, SNR=%2d. ",
                cmd.satellites[i].prn, cmd.satellites[i].snr);
    }
    LOG_DEBUG("%s\n", buff);
#endif
}


void GpsAiroha::onSyncRaw(const Interface::IImu::SyncSampleData* data, size_t count, float accelScale, float gyroScale,
                          uint32_t periodUs)
{
    (void)periodUs;

    if (!mImu) {
        return;
    }

//    LOG_INFO("Sync: %d samples, accel scale %.6f, gyro  scale %.6f\n", count, accelScale, gyroScale);
//    if (count > 0) {
//        LOG_INFO("  [%3d] t=%.1f ax=%d ay=%d az=%d gx=%d gy=%d gz=%d\n", 0,
//                data[0].timestamp / 1000.0f,
//                data[0].accel_x, data[0].accel_y, data[0].accel_z,
//                data[0].gyro_x, data[0].gyro_y, data[0].gyro_z);
//        if (count > 1) {
//            uint16_t last = count - 1;
//            LOG_INFO("  [%3d] t=%.1f, Delta: %.1f ms\n", last, data[last].timestamp / 1000.0f,
//                      (data[last].timestamp - data[0].timestamp) / 1000.0f);
//            (void)last;
//        }
//    }

    for (size_t i = 0; i < count; i++) {
        float ax = data[i].accel_x * accelScale; // m/s2
        float ay = data[i].accel_y * accelScale; // m/s2
        float az = data[i].accel_z * accelScale; // m/s2

        float gx = data[i].gyro_x * gyroScale; // rad/s
        float gy = data[i].gyro_y * gyroScale; // rad/s
        float gz = data[i].gyro_z * gyroScale; // rad/s

        mImuLogToFile.writef("%g,%g,%g,%g,%g,%g,%g\n",
                data[i].timestamp / 1000.0f, ax, ay, az, gx, gy, gz);
    }

}

} // namespace Gps
