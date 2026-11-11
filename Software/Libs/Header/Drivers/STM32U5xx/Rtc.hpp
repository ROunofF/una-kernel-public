/**
 ******************************************************************************
 * @file    Rtc.hpp
 * @brief   Real Time Clock (RTC) driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __DRIVER_RTC_HPP
#define __DRIVER_RTC_HPP

#include <stdint.h>
#include <stdbool.h>

#include "IRtc.hpp"

#include "main.h"


namespace Driver
{

/**
 * @brief   RTC driver.
 */
class Rtc : virtual public Interface::IRtc {

public:

    /**
     * @brief   Get RTC driver instance.
     * @retval  Pointer to the driver instance.
     */
    static Rtc& GetInstance()
    {
        static Rtc sInstance;
        return sInstance;
    }

    /**
     * @brief   Initialization of the RTC instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool init();

    /**
     * @brief   Deinitialization of the RTC instance.
     */
    void deinit();

    /**
     * @brief   Get timestamp in ms.
     * @note    The maximum value of this counter is 86400000 ms (1 day).
     *          It is used to correct RTOS ticks after sleep.
     */
    uint32_t getTimeMs();

    /**
     * @brief   Set new relative time offset for wake up event basing on
     *          current time.
     * @param   ms: Time offset from current time point to alarm event
     *          measured in milliseconds. Maximum time is 30s
     * @retval  Execution status. 'true' - if success, 'false' - otherwise.
     */
    bool setWakeUpTimer(uint32_t ms);

    constexpr uint32_t getMaxWakeUpTime() const
    {
        return skMaxSleepTimeMs;
    }

    /**
     * @brief   Write a data in a specified RTC Backup data register.
     * @param   reg: RTC Backup data Register number.
     *          This parameter can be can be from RTC_BKP_DR5 to RTC_BACKUP_NB
     * @param   value: Data to be written in the specified Backup data register.
     */
    virtual void writeBackupReg(uint32_t reg, uint32_t value) override;

    /**
     * @brief   Reads data from the specified RTC Backup data Register.
     * @param   reg: RTC Backup data Register number.
     *          This parameter can be can be from RTC_BKP_DR5 to RTC_BACKUP_NB
     * @retval  Read value
     */
    virtual uint32_t readBackupReg(uint32_t reg) override;

    /**
     * @copydoc Interface::IRtc::get()
     */
    virtual Interface::IRtc::DateTime get() override;

    /**
     * @copydoc Interface::IRtc::set()
     */
    virtual bool set(const std::tm &dateTime) override;

    /**
     * @brief   Whether the time has been set since the backup domain
     *          last lost power. Same flag as DateTime::time_reliable,
     *          without reading the calendar.
     */
    bool isTimeSet();

    /**
     * @brief   Whether the time zone has been set since the backup domain
     *          last lost power. Until then getTimeZone() returns 0 and
     *          local time is UTC.
     */
    bool isTimeZoneSet();

    /**
     * @copydoc Interface::IRtc::getTimeZone()
     */
    virtual int getTimeZone() override;

    /**
     * @copydoc Interface::IRtc::setTimeZone()
     */
    virtual void setTimeZone(int tz) override;

    /**
     * @copydoc Interface::IRtc::getDstOffset()
     */
    virtual int getDstOffset() override;

    /**
     * @copydoc Interface::IRtc::setDstOffset()
     */
    virtual void setDstOffset(int dstoff) override;

    /**
     * @brief   Interrupt handlers for this driver.
     * @note    Call this method from stm32u5xx_it.c/cpp file.
     */
    static void IrqHandler();

private:

    // HAL RTC object
    RTC_HandleTypeDef mRtcHandle { };

    bool mIsInitialized { };

    // Maximum sleep time in ms.
    static constexpr uint32_t skMaxSleepTimeMs = 30000;

    static constexpr uint32_t skTimeBkpReg     = RTC_BRK_REG_TIME;
    static constexpr uint32_t skTimezoneBkpReg = RTC_BRK_REG_TIMEZONE;
    static constexpr uint32_t skDSTBkpReg      = RTC_BRK_REG_DST;

    static constexpr uint16_t skMagigNumber = 0x32F2;
    struct BackupValue {
        union {
            uint32_t raw;
            struct {
                uint16_t magic;
                int16_t value;
            };
        };
    };

    /**
     * @brief Constructor.
     */
    Rtc() = default;

    /**
     * @brief Destructor.
     */
    virtual ~Rtc()
    {
        deinit();
    }

    int weekdayFromYMD(int y, int m, int d);

    // HAL callbacks
    static void MspDeInitCallback(RTC_HandleTypeDef *rtcHandle);
    static void MspInitCallback(RTC_HandleTypeDef *rtcHandle);
    static void WakeUpTimerEventCallback(RTC_HandleTypeDef *rtcHandle);

};

} /* namespace Driver */

#endif /* __DRIVER_RTC_HPP */
