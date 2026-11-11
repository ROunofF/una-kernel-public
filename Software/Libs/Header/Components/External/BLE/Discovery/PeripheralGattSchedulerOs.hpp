/**
 ******************************************************************************
 * @file    PeripheralGattSchedulerOs.hpp
 * @brief   OS-timer scheduler for the peripheral-link GATT-client coordinator.
 ******************************************************************************
 *
 * Backs the peripheral coordinator's one-shot delay requests with an OS timer
 * and ticks it on expiry. Confines the OS-timer dependency to the firmware
 * build so the coordinator itself stays OS-free (and host-testable via a mock
 * IGattScheduler). A central-link coordinator provides its own scheduler.
 *
 ******************************************************************************
 */

#ifndef BLE_DISCOVERY_PERIPHERAL_GATT_SCHEDULER_OS_HPP
#define BLE_DISCOVERY_PERIPHERAL_GATT_SCHEDULER_OS_HPP

#include "IGattScheduler.hpp"
#include "IBleThreadWork.hpp"
#include "OS.hpp"

namespace Ble
{

class PeripheralLinkClients;

/**
 * @brief   One-shot OS-timer scheduler that ticks the peripheral coordinator.
 * @note    The timer fires on the RTOS timer-daemon task; the tick itself
 *          mutates coordinator state and issues blocking GATT commands, so it is
 *          marshalled onto the BLE event thread rather than run there.
 */
class PeripheralGattSchedulerOs : public IGattScheduler {
public:

    /**
     * @brief   Create the timer and bind the coordinator it ticks.
     * @param   name: timer name for diagnostics.
     * @param   manager: peripheral coordinator to tick on expiry.
     * @param   work: port that runs the tick on the BLE event thread.
     */
    void init(const char* name, PeripheralLinkClients* manager,
            IBleThreadWork* work);

    void arm(uint32_t ms) override;
    void cancel() override;

private:
    ///< Re-arm delay when the BLE work queue is momentarily full.
    static constexpr uint32_t kRepostMs = 20;

    PeripheralLinkClients* mManager { nullptr };
    IBleThreadWork* mpWork { nullptr };
    OS::Timer<PeripheralGattSchedulerOs> mTimer;

    void timCallback(void* arg);
    // Runs on the BLE event thread: ticks the bound coordinator.
    static void tickTrampoline(void* ctx);
};

} // namespace Ble

#endif // BLE_DISCOVERY_PERIPHERAL_GATT_SCHEDULER_OS_HPP
