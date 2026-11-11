/**
 ******************************************************************************
 * @file    CurrentTimeService.hpp
 * @brief   BLE Current Time Service (CTS).
 *
 *          Description: "GATT Specification Supplement"
 *          (https://www.bluetooth.com/specifications/specs/gatt-specification-supplement/)
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_SERVICE_CTS_HPP
#define __COMPONENT_BLE_SERVICE_CTS_HPP

#include "IBle.hpp"
#include <ctime>

namespace Ble
{

/**
 * @brief   BLE Current Time Service (CTS).
 */
class CurrentTimeService : virtual public Interface::IBle::CharacteristicCallback {
public:

    class Callback {
    public:
        virtual void onWriteTime(std::tm localTime){}
        virtual void onReadTime(std::tm &localTime){}
        virtual void onWriteTimeZoneDST(int16_t tz, uint16_t dstOffset){}
        virtual void onReadTimeZoneDST(int16_t &tz, uint16_t &dstOffset){}
    protected:
        virtual ~Callback() = default;
    };

    /**
     * @brief   Constructor.
     * @param   ble: BLE instance.
     * @param   pCallback: Time operation callback.
     */
    CurrentTimeService(Interface::IBle &ble, Callback *pCallback, bool useTimeZone = false);

    /**
     * @brief   Destructor.
     */
    virtual ~CurrentTimeService();


    /**
     * @brief   Time Adjust Reason bitmask.
     */
    struct AdjustReason {
        static constexpr uint8_t MANUAL = 0x01;     ///< Bit 0: Manual time update
        static constexpr uint8_t EXT_REF = 0x02;    ///< Bit 1: External reference time update
        static constexpr uint8_t TZ_CHG = 0x04;     ///< Bit 2: Time zone change
        static constexpr uint8_t DST_CHG = 0x08;    ///< Bit 3: DST change
    };

    /**
     * @brief   Set Time Adjust Reason and send notification to a BLE client.
     * @param   reason: Time Adjust Reason bitmask.
     */
    void onTimeAdjust(uint8_t reason);

private:

    Interface::IBle &mBle;

    ServiceHandle mSrv {};
    CharacteristicHandle mChrCurrentTime {};
    CharacteristicHandle mChrLocalTime {};

    bool mNotificationEnabled = false;
    uint8_t mTimeAdjustReasonReq = 0;

    Callback *mObserver = nullptr;

    /**
     * @brief    Current Time Characteristic (0x2A2B)
     */
    struct CurrentTime {
        /**
         * @brief  Year as defined by the Gregorian calendar.
         * @details Valid range: 1582 to 9999. A value of 0 means that the year is not known.
         *          All other values are Reserved for Future Use.
         * @details 16-bit, little-endian
         */
        uint16_t year;

        /**
         * @brief  Month of the year as defined by the Gregorian calendar.
         * @details Valid range: 1 (January) to 12 (December). A value of 0 means that the month
         *          is not known. All other values are Reserved for Future Use.
         */
        uint8_t month;

        /**
         * @brief  Day of the month as defined by the Gregorian calendar.
         * @details Valid range: 1 to 31. A value of 0 means that the day of the month is not known.
         *          All other values are Reserved for Future Use.
         */
        uint8_t day;

        /**
         * @brief  Number of hours past midnight.
         * @details Valid range: 0 to 23. All other values are Reserved for Future Use.
         */
        uint8_t hours;

        /**
         * @brief  Number of minutes since the start of the hour.
         * @details Valid range: 0 to 59. All other values are Reserved for Future Use.
         */
        uint8_t minutes;

        /**
         * @brief  Number of seconds since the start of the minute.
         * @details Valid range: 0 to 59. All other values are Reserved for Future Use.
         */
        uint8_t seconds;

        /**
         * @brief  Day of the week.
         * @details Valid values: 1–7 (where 1 = Monday). A value of 0 indicates Unknown.
         */
        uint8_t dayOfWeek;

        /**
         * @brief  Fractional part of a second.
         * @details Measured in 1/256 increments.
         */
        uint8_t fractions256;

        /**
         * @brief  Adjustment reason bitfield.
         * @details Indicates why the time was adjusted.
         */
        uint8_t adjustReason;
    } __attribute__((packed));





    /**
     * @brief    Local Time Information Characteristic (0x2A0F)
     */
    struct LocalTime {
        int8_t tz;      ///< Time Zone: (-48 to 56, representing offset in 15-minute intervals from UTC)
        uint8_t dst;    ///< DST Offset
    } __attribute__((packed));


    enum DST {
        DST_STANDART = 0x00,    ///< Standard Time
        DST_DAYLIGHT_05 = 0x02, ///< Half an hour Daylight Time (+ 0.5h)
        DST_DAYLIGHT_1 = 0x04,  ///< Daylight Time (+ 1h)
        DST_DAYLIGHT_2 = 0x08,  ///< Double Daylight Time (+ 2h)
        DST_UNKNOWN = 0xFF,     ///< DST unknown
        // All other values: Reserved for Future Use
    };


    /**
     * @brief    Reference Time Information Characteristic (0x2A14)
     */
    struct ReferenceTime {
        uint8_t source;             ///< Time Source
        uint8_t accuracy;           ///< Accuracy of the time source in seconds
        uint8_t daysSinceUpdate;    ///< Days Since Update: (0–255, number of days since last update)
        uint8_t hoursSinceUpdate;   ///< Hours Since Update: (0–23, number of hours since last update)
    } __attribute__((packed));

    enum TimeSource {
        TIME_SRC_UNKNOWN = 0,       ///< Unknown
        TIME_SRC_NTP = 1,           ///< Network Time Protocol
        TIME_SRC_GPS = 2,           ///< GPS
        TIME_SRC_RADIO = 3,         ///< Radio
        TIME_SRC_MANUAL = 4,        ///< Manual
        TIME_SRC_ATOMIC_CLOCK = 5,  ///< Atomic Clock
        TIME_SRC_CELLULAR = 6,      ///< Cellular Network
    };


    /**
     * @brief   Called when a characteristic's notification or indication
     *          is enabled/disabled.
     * @param   chrHandle: Handle of the characteristic.
     * @param   notify: 'true' if notifications are enabled, 'false' disabled.
     * @param   indicate:'true' if indications are enabled, 'false' disabled.
     */
    virtual void onNotifyIndicate(Ble::CharacteristicHandle chrHandle,
            bool notify, bool indicate) override;

    /**
     * @brief   Called when data is written to a characteristic by the
     *          remote device.
     * @param   chrHandle: Handle of the characteristic being written to.
     * @param   pData: Pointer to the data that was written.
     * @param   len: Length of the data in bytes.
     */
    virtual void onWrite(Ble::CharacteristicHandle chrHandle,
            const uint8_t *pData, uint16_t len) override;

    /**
     * @brief   Called when the remote device reads a characteristic's value.
     * @param   chrHandle - Handle of the characteristic being read.
     */
    virtual void onRead(Ble::CharacteristicHandle chrHandle) override;

    /**
     * @brief   Fill CurrentTime structure with local time.
     * @param   ct: CurrentTime structure.
     */
    void fillCurrentTime(CurrentTime &ct);

};

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_SERVICE_CTS_HPP */
