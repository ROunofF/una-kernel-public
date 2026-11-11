/**
 ******************************************************************************
 * @file    TimerSysMs.c
 * @brief   Software timer component.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "TimerSysMs.h"
#include <time.h>

////////////////////////////////////////////////////////////////////////////////
/// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Private data
////////////////////////////////////////////////////////////////////////////////

static ITimerSysMs_getTicks_t mGetTicks;

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Assigns a function through which TimerSysMs will be able to get system ticks
 * 
 * @param cb Pointer to a function for getting system ticks
 */
void TimerSysMs_init(ITimerSysMs_getTicks_t cb)
{
    mGetTicks = cb;
}

/**
 * @brief Activates or deactivates a timer
 * 
 * @param timer Pointer to TimerSysMs_t object
 * @param value True - activates, false - deactivates
 */
void TimerSysMs_setActive(TimerSysMs_t* timer, bool value)
{
    timer->active = value;
}

/**
 * @brief Returns current value of activity
 * 
 * @param timer Pointer to TimerSysMs_t object
 * @return      True if timer is active, false otherwise
 */
bool TimerSysMs_getActive(TimerSysMs_t* timer)
{
    return timer->active;
}

/**
 * @brief Sets the new value of the timer interval
 * 
 * @param timer Pointer to TimerSysMs_t object
 * @param value Timer interval (in ms)
 */
void TimerSysMs_setInterval(TimerSysMs_t* timer, uint32_t value)
{
    timer->interval = value;
}

/**
 * @brief Returns the current value of the timer interval
 * 
 * @param timer Pointer to TimerSysMs_t object
 * @return uint32_t 
 */
uint32_t TimerSysMs_getInterval(TimerSysMs_t* timer)
{
    return timer->interval;
}

/**
 * @brief Starts a timer
 * 
 * @warning Timer interval must be set early by TimerSysMs_setInterval function
 * @param timer Pointer to TimerSysMs_t object
 */
void TimerSysMs_start(TimerSysMs_t* timer)
{
    timer->prevSysTimer = mGetTicks();
    timer->active = true;
}

/**
 * @brief Sets an interval for a timer and starts it
 * 
 * @param timer Pointer to TimerSysMs_t object
 * @param value Interval (in ms) of a timer
 */
void TimerSysMs_startWithValue(TimerSysMs_t* timer, uint32_t value)
{
    TimerSysMs_setInterval(timer, value);
    TimerSysMs_start(timer);
}

/**
 * @brief Stops a timer
 * 
 * @param timer Pointer to TimerSysMs_t object
 */
void TimerSysMs_stop(TimerSysMs_t* timer)
{
    TimerSysMs_setActive(timer, false);
}

/**
 * @brief Resets a timer
 * 
 * @param timer Pointer to TimerSysMs_t object
 */
void TimerSysMs_reset(TimerSysMs_t* timer)
{
    timer->prevSysTimer = mGetTicks();
}

/**
 * @brief Waits for specified amount of milliseconds
 * 
 * @param ms_number Amount of milliseconds
 */
void TimerSysMs_wait(uint32_t ms_number)
{
    uint32_t ts = mGetTicks();

    while (TimerSysMs_passedNow(mGetTicks(), ts) < ms_number) {
    }
}

/**
 * @brief Returns current timestamp
 * 
 * @return uint32_t Current timestamp
 */
uint32_t TimerSysMs_getTicks(void)
{
    return mGetTicks();
}

/**
 * @brief Returns amount of milliseconds from start or previous expiring
 * 
 * @param timer Pointer to TimerSysMs_t object
 * @return      Amount of milliseconds from start or previous expiring
 */
uint32_t TimerSysMs_getTime(TimerSysMs_t* timer) 
{
    return TimerSysMs_passed(timer->prevSysTimer);
}

/**
 * @brief Checks if a timer was expired
 * 
 * @param timer Pointer to TimerSysMs_t object
 * @return      True if a timer was expired, false otherwise
 */
bool TimerSysMs_check(TimerSysMs_t* timer)
{
    if (timer->active == false)
        return false;

    uint32_t now = mGetTicks();
    bool     res = (TimerSysMs_passedNow(now, timer->prevSysTimer) >= timer->interval);

    if (res == true) {
        timer->prevSysTimer = now;
    }

    return res;
};

/**
 * @brief Returns the number of milliseconds between two timestamp values
 * 
 * @details If a newer value of the timestamp is less than value of the 
 *          older timestamp, this means that the time counter has been 
 *          overflowed and the calculation of the number of milliseconds
 *          will take this into account.
 * 
 * @param now        Newer timestamp
 * @param time_stamp Older timestamp
 * @return uint32_t  Amount of milliseconds
 */
uint32_t TimerSysMs_passedNow(uint32_t now, uint32_t time_stamp)
{
    uint32_t result = (uint32_t)(-1);

    if (now >= time_stamp)
        result = now - time_stamp;
    else
        result = (result - time_stamp) + now + 1;

    return result;
}

/**
 * @brief Returns the number of milliseconds that have passed from the previous timestamp
 * 
 * @param time_stamp Previous timestamp
 * @return uint32_t  Amount of milliseconds
 */
uint32_t TimerSysMs_passed(uint32_t time_stamp)
{
    return TimerSysMs_passedNow(mGetTicks(), time_stamp);
}
