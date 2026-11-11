/**
 ******************************************************************************
 * @file    IRtc.hpp
 * @brief   Real Time Clock (RTC) interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __INTERFACE_I_RTC_HPP
#define __INTERFACE_I_RTC_HPP

#include <cstdint>
#include <cstdbool>
#include <ctime>

namespace Interface
{

/**
 * @brief   Real Time Clock (RTC) interface.
 */
class IRtc {

public:

    /**
     * @brief Structure representing date and time.
     * @note  Extends the standard 'tm' structure from '<ctime>'.
     *
     * This structure provides detailed information about the current date and time,
     * including support for milliseconds, Daylight Saving Time (DST) and time zone offsets.
     * It extends the standard 'tm' structure by adding additional fields for finer granularity
     * and handling specific time-related information.
     *
     * Members:
     *  - tm_hour    Hours since midnight (0-23)
     *  - tm_min     Minutes after the hour (0-59)
     *  - tm_sec     Seconds after the minute (0-59)
     *
     *  - tm_mday    Day of the month (1-31)
     *  - tm_mon     Month (0-11; January = 0)
     *  - tm_year    Year (current year minus 1900)
     *
     *  - tm_wday    Day of the week (0-6; Sunday = 0)
     *  - tm_yday    Day of the year (0-365; January 1 = 0)
     *
     *  - tm_isdst   Positive if daylight saving time is in effect;
     *               0 if daylight saving time isn't in effect;
     *               negative if the status of daylight saving time is unknown.
     *               The C run-time library assumes the United States' rules
     *               for implementing the calculation of Daylight Saving Time (DST).
     *
     *  - ms         Milliseconds
     *
     *  - dst_offset Daylight Saving Time offset in seconds.
     *               -1 the offset of daylight saving time is unknown.
     *
     *  - timezone   Timezone offset in seconds.
     *
     *  - time_reliable Determines whether the time is reliable, i.e. it was set once
     *               and the RTC has not been turned off.
     *               1 - time reliable. 0 - time not reliable.
     */
    struct DateTime : public std::tm {
        int ms;             ///< Milliseconds
        int dst_offset;     ///< Daylight Saving Time offset in seconds
        int timezone;       ///< Timezone offset in seconds
        int time_reliable;  ///< Determines whether the time is reliable
    };

    /**
     * @brief Get RTC date and time.
     * @return Extended time structure.
     */
    virtual Interface::IRtc::DateTime get() = 0;

    /**
     * @brief Set RTC date and time.
     * @param dateTime: Structure containing the date and time to set.
     * @retval Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool set(const std::tm &dateTime) = 0;

    /**
     * @brief Gets the current timezone offset.
     * @return Timezone offset in seconds.
     */
    virtual int getTimeZone() = 0;

    /**
     * @brief Sets the timezone offset.
     * @param tz Timezone offset in seconds.
     */
    virtual void setTimeZone(int tz) = 0;

    /**
     * @brief Gets the daylight saving time offset.
     * @return DST offset in seconds.
     */
    virtual int getDstOffset() = 0;

    /**
     * @brief Sets the daylight saving time offset.
     * @param dstoff DST offset in seconds.
     */
    virtual void setDstOffset(int dstoff) = 0;

    /**
     * @brief   Write a data in a specified RTC Backup data register.
     * @param   reg: RTC Backup data Register number.
     *          This parameter can be can be from RTC_BKP_DR5 to RTC_BACKUP_NB
     * @param   value: Data to be written in the specified Backup data register.
     */
    virtual void writeBackupReg(uint32_t reg, uint32_t value) {}

    /**
     * @brief   Reads data from the specified RTC Backup data Register.
     * @param   reg: RTC Backup data Register number.
     *          This parameter can be can be from RTC_BKP_DR5 to RTC_BACKUP_NB
     * @retval  Read value
     */
    virtual uint32_t readBackupReg(uint32_t reg) { return 0; }

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~IRtc() = default;

};

} /* namespace Interface */

#endif /* __INTERFACE_I_RTC_HPP */
