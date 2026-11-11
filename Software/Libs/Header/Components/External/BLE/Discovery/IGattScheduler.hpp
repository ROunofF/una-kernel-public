/**
 ******************************************************************************
 * @file    IGattScheduler.hpp
 * @brief   One-shot delayed-callback port for the GATT-client coordinator.
 ******************************************************************************
 *
 * Discovery steps are paced with short delays so the controller settles a
 * procedure before the next is issued. The coordinator requests a delay through
 * this port instead of owning an OS timer directly, which keeps it free of OS
 * dependencies and lets host unit tests fire the tick deterministically.
 *
 ******************************************************************************
 */

#ifndef BLE_DISCOVERY_IGATT_SCHEDULER_HPP
#define BLE_DISCOVERY_IGATT_SCHEDULER_HPP

#include <cstdint>

namespace Ble
{

/**
 * @brief   One-shot timer port. On expiry the implementation ticks the bound
 *          coordinator. Re-arming before expiry restarts the single timer.
 */
class IGattScheduler {
public:

    /**
     * @brief   Arm the one-shot timer.
     * @param   ms: delay in milliseconds before the bound coordinator is ticked.
     */
    virtual void arm(uint32_t ms) = 0;

    /**
     * @brief   Cancel a pending tick (no effect if the timer is not armed).
     */
    virtual void cancel() = 0;

protected:
    virtual ~IGattScheduler() = default;
};

} // namespace Ble

#endif // BLE_DISCOVERY_IGATT_SCHEDULER_HPP
