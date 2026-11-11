/**
 ******************************************************************************
 * @file    PeripheralGattSchedulerOs.cpp
 * @brief   OS-timer scheduler for the peripheral-link GATT-client coordinator.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "BLE.GattSched"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "PeripheralGattSchedulerOs.hpp"

#include "PeripheralLinkClients.hpp"

namespace Ble
{

void PeripheralGattSchedulerOs::init(const char* name,
        PeripheralLinkClients* manager, IBleThreadWork* work)
{
    mManager = manager;
    mpWork = work;
    mTimer.init(name, this, &PeripheralGattSchedulerOs::timCallback, false, this);
}

void PeripheralGattSchedulerOs::arm(uint32_t ms)
{
    mTimer.start(ms);
}

void PeripheralGattSchedulerOs::cancel()
{
    mTimer.stop();
}

void PeripheralGattSchedulerOs::timCallback(void* arg)
{
    (void) arg;
    if (mManager == nullptr) {
        return;
    }

    // The timer fires on the RTOS timer-daemon task. The tick mutates
    // coordinator state and issues blocking GATT commands, so hand it to the BLE
    // event thread. If the work queue is momentarily full, re-arm and retry.
    if (mpWork != nullptr) {
        if (!mpWork->post(&PeripheralGattSchedulerOs::tickTrampoline, mManager)) {
            mTimer.start(kRepostMs);
        }
        return;
    }

    // No work port bound (host tests drive onTick() directly): tick in place.
    mManager->onTick();
}

void PeripheralGattSchedulerOs::tickTrampoline(void* ctx)
{
    static_cast<PeripheralLinkClients*>(ctx)->onTick();
}

} // namespace Ble
