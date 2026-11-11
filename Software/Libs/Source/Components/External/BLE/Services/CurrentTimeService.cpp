/**
 ******************************************************************************
 * @file    CurrentTimeService.cpp
 * @brief   BLE Current Time Service (CTS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Services/CurrentTimeService.hpp"

#define LOG_MODULE_PRX      "BLE.SRV.CurrentTime"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace Ble
{

CurrentTimeService::CurrentTimeService(Interface::IBle &ble,
        Callback *pCallback, bool useTimeZone) :
        mBle(ble), mObserver(pCallback)
{
    // Create the CTS service
    mSrv = mBle.addService(UUID(0x1805));

    // Add Current Time characteristic
    mChrCurrentTime = mBle.addCharacteristic(mSrv, UUID(0x2A2B),
            sizeof(CurrentTime),
            (CharProp::READ | CharProp::WRITE | CharProp::NOTIFY));
    mBle.attachCharacteristicCallback(mChrCurrentTime, this);

    // Add Local Time Information characteristic
    if (useTimeZone) {
        mChrLocalTime = mBle.addCharacteristic(mSrv, UUID(0x2A0F),
                sizeof(LocalTime), (CharProp::READ | CharProp::WRITE));
        mBle.attachCharacteristicCallback(mChrLocalTime, this);
    }
}

CurrentTimeService::~CurrentTimeService()
{
    if (mChrLocalTime) {
        mBle.attachCharacteristicCallback(mChrLocalTime, nullptr);
        mBle.delCharacteristic(mChrLocalTime);
    }
    mBle.attachCharacteristicCallback(mChrCurrentTime, nullptr);
    mBle.delCharacteristic(mChrCurrentTime);

    mBle.delService(mSrv);
}

void CurrentTimeService::onTimeAdjust(uint8_t reason)
{
    if ((reason & 0x0F) != 0) {
        mTimeAdjustReasonReq |= (reason & 0x0F);

        if (mNotificationEnabled) {
            CurrentTime ct { };
            fillCurrentTime(ct);

            LOG_INFO("Date: %04u-%02u-%02u, Weekday: %u, Time: %02u:%02u:%02u\n",
                    ct.year, ct.month, ct.day, ct.dayOfWeek, ct.hours, ct.minutes, ct.seconds);

            mBle.setCharacteristicValue(mChrCurrentTime,
                    (const uint8_t*) &ct, sizeof(CurrentTime));

            mTimeAdjustReasonReq = 0;
        }
    }
}

void CurrentTimeService::onNotifyIndicate(Ble::CharacteristicHandle chrHandle,
        bool notify, bool indicate)
{
    if (chrHandle == mChrCurrentTime) {
        mNotificationEnabled = notify;
        LOG_DEBUG("Notification %s\n",mNotificationEnabled ? "enabled" : "disabled");
        if (mNotificationEnabled && mTimeAdjustReasonReq != 0) {
            CurrentTime ct { };
            fillCurrentTime(ct);
            mBle.setCharacteristicValue(mChrCurrentTime,
                    (const uint8_t*) &ct, sizeof(CurrentTime));

            mTimeAdjustReasonReq = 0;
        }
    }
}

void CurrentTimeService::onWrite(Ble::CharacteristicHandle chrHandle,
        const uint8_t *pData, uint16_t len)
{
    char tbuff[37];

    if (chrHandle == mChrCurrentTime) {
        const CurrentTime &ct = *(const CurrentTime*) pData;

        LOG_INFO("Date: %04u-%02u-%02u, Weekday: %u, Time: %02u:%02u:%02u\n",
                ct.year, ct.month, ct.day, ct.dayOfWeek, ct.hours, ct.minutes, ct.seconds);

        std::tm ldt { };

        // Validate length and date
        if (len == sizeof(CurrentTime)
                && ct.year  >= 1900
                && ct.month > 0
                && ct.day > 0
                && ct.dayOfWeek > 0)
        {

            ldt.tm_year = ct.year - 1900;
            ldt.tm_mon = ct.month - 1;
            ldt.tm_mday = ct.day;

            ldt.tm_wday = (ct.dayOfWeek == 7) ? 0 : ct.dayOfWeek;

            ldt.tm_hour = ct.hours;
            ldt.tm_min = ct.minutes;
            ldt.tm_sec = ct.seconds;

            if (mObserver) {
                mObserver->onWriteTime(ldt);
            }

            // Request not relevant
            mTimeAdjustReasonReq = 0;
        } else {
            LOG_WARNING("Wrong date/time format. UUID: %s",
                    mBle.getCharacteristicUUID(chrHandle).toString(tbuff));
            LOG_WARNING_DUMP(pData, len);
        }

    }

    if (chrHandle == mChrLocalTime) {
        const LocalTime &lt = *(const LocalTime*)pData;
        LOG_INFO("Time zone: 0x%02x (%d min), DST: 0x%02X (%d min)\n",
                lt.tz, lt.tz * 15, lt.dst, lt.dst * 15);

        // Validate length, TZ and DST
        if (len == sizeof(LocalTime)
            && (lt.tz >= -48 && lt.tz <= 56)    // unknown TZ not valid for setting
            && (lt.dst == DST_STANDART
                || lt.dst == DST_DAYLIGHT_05
                || lt.dst == DST_DAYLIGHT_1
                || lt.dst == DST_DAYLIGHT_2))   // unknown DST not valid for setting
        {
            if (mObserver) {
                mObserver->onWriteTimeZoneDST(lt.tz * 15 * 60, lt.dst * 15 * 60);
            }
        } else {
            LOG_WARNING("Wrong TZ/DST format. UUID: %s",
                    mBle.getCharacteristicUUID(chrHandle).toString(tbuff));
            LOG_WARNING_DUMP(pData, len);
        }
    }

}

void CurrentTimeService::onRead(Ble::CharacteristicHandle chrHandle)
{
    if (chrHandle == mChrCurrentTime) {
        CurrentTime ct {};
        fillCurrentTime(ct);

        LOG_INFO("Date: %04u-%02u-%02u, Weekday: %u, Time: %02u:%02u:%02u\n",
                ct.year, ct.month, ct.day, ct.dayOfWeek, ct.hours, ct.minutes, ct.seconds);

        mBle.setCharacteristicValue(mChrCurrentTime,
                    (const uint8_t*)&ct, sizeof(CurrentTime));
        // Clear request after time reading
        mTimeAdjustReasonReq = 0;
    }

    if (mChrLocalTime && chrHandle == mChrLocalTime) {
        LocalTime lt {};

        if (mObserver) {
            int16_t tz = 0;
            uint16_t dstOffset = 0;
            mObserver->onReadTimeZoneDST(tz, dstOffset);
            lt.tz = tz / (15 * 60);
            lt.dst = dstOffset / (15 * 60);
        }

        LOG_INFO("Time zone: 0x%02x (%d min), DST: 0x%02X (%d min)\n",
                        lt.tz, lt.tz * 15, lt.dst, lt.dst * 15);

        mBle.setCharacteristicValue(mChrLocalTime,
                    (const uint8_t*)&lt, sizeof(LocalTime));
    }


}

void CurrentTimeService::fillCurrentTime(CurrentTime &ct)
{
    std::tm ldt { };
    if (mObserver) {
        mObserver->onReadTime(ldt);
    }

    ct.year = ldt.tm_year + 1900;
    ct.month = ldt.tm_mon + 1;
    ct.day = ldt.tm_mday;

    ct.dayOfWeek = (ldt.tm_wday == 0) ? 7 : ldt.tm_wday;

    ct.hours = ldt.tm_hour;
    ct.minutes = ldt.tm_min;
    ct.seconds = ldt.tm_sec;

    ct.adjustReason = mTimeAdjustReasonReq;

}

}  /* namespace Ble */
