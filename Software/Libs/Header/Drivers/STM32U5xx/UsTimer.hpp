/**
 ******************************************************************************
 * @file    UsTimer.hpp
 * @brief   Provides methods for working with internal us timer
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __DRIVER_US_TIMER_HPP
#define __DRIVER_US_TIMER_HPP

#include <cstdint>
#include <cstdbool>

#include "main.h"

namespace Driver
{

#define DWT_CYCCNT    *(volatile uint32_t *)0xE0001004
#define DWT_CONTROL   *(volatile uint32_t *)0xE0001000
#define SCB_DEMCR     *(volatile uint32_t *)0xE000EDFC

/**
 * @brief   Description
 */
class UsTimer {

public:

    /**
     * @brief   Initialize driver instance.
     */
    static void Init()
    {
        if (!(DWT_CONTROL & 1) || !(SCB_DEMCR & 0x01000000)) {
            SCB_DEMCR |= 0x01000000;    // enable the counter
            DWT_CYCCNT = 0;             // reset the counter
            DWT_CONTROL |= 1;           // start the counter
        }
    }

    /**
     * @brief   Delay for microseconds.
     * @param   us: time to delay in microseconds.
     */
    static void DelayUs(uint32_t us)
    {
        uint32_t start = DWT_CYCCNT;
        uint32_t ticks = us * (SystemCoreClock / 1000000);
        while ((uint32_t)(DWT_CYCCNT - start) < ticks);
    }

    /**
     * @brief   Get microseconds.
     * @retval  Time in microseconds.
     */
    static uint32_t GetUs(void)
    {
        return DWT_CYCCNT / (SystemCoreClock / 1000000);
    }

    /**
     * @brief   Get maximum microseconds value.
     * @retval  Maximum microseconds value to overflow.
     */
    static uint32_t GetOverflowValue()
    {
        return 0xFFFFFFFF / (SystemCoreClock / 1000000);
    }

    /**
     * @brief   Get timer counter register.
     * @retval  Timer ticks.
     */
    static uint32_t GetTicks()
    {
        return DWT_CYCCNT;
    }

    /**
     * @brief   Get clock frequency
     * @retval  Timer clock.
     */
    static uint32_t CetClockFrequency()
    {
        return SystemCoreClock;
    }

private:

    /**
     * @brief   Constructor.
     */
    UsTimer() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~UsTimer() = default;

};

} /* namespace Drivers */

#endif /* __DRIVER_US_TIMER_HPP */
