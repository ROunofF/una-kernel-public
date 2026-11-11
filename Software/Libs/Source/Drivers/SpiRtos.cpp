/**
 ******************************************************************************
 * @file    SpiRtos.cpp
 * @brief   Thread-safe RTOS wrapper for SPI driver.
 ******************************************************************************
 */

#include "SpiRtos.hpp"

#define LOG_MODULE_PRX      "SpiRtos"
#define LOG_MODULE_LEVEL    LOG_LEVEL_WARNING
#include "Logger.h"

#include <cassert>
#include "main.h"


#include "Sleep.hpp"

namespace Driver
{

SpiRtos::SpiRtos(Interface::ISpi *pObj, uint32_t timeout)
{
    assert(pObj != nullptr);
    config(pObj, timeout);
}

SpiRtos::~SpiRtos()
{
    if (mpObj != nullptr) {
        mpObj->setCpltHandler(nullptr);
    }

    if (mMutHandle != nullptr) {
        osMutexDelete(mMutHandle);
    }
}

void SpiRtos::config(Interface::ISpi *pObj, uint32_t timeout)
{
    assert(pObj != nullptr);

    mTimeout = (timeout == 0) ? 1 : timeout;
    mpObj    = pObj;
    mpObj->setCpltHandler(this);

    if (mMutHandle == nullptr) {
        const osMutexAttr_t kMutAttributes = {
            .name     = "SpiRtos",
            .attr_bits = 0,
            .cb_mem   = &mMutControlBlock,
            .cb_size  = sizeof(mMutControlBlock)
        };

        mMutHandle = osMutexNew(&kMutAttributes);
    }
}

bool SpiRtos::init()
{
    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    Driver::SleepGuard guard;
    status = mpObj->init();

    mutexRelease();

    return status;
}

void SpiRtos::deinit()
{
    assert(mpObj != nullptr);

    if (!mutexLock(mTimeout)) {
        return;
    }

    Driver::SleepGuard guard;
    mpObj->deinit();

    mutexRelease();
}

void SpiRtos::setMode(Interface::ISpi::Mode mode)
{
    assert(mpObj != nullptr);

    if (!mutexLock(mTimeout)) {
        return;
    }

    mpObj->setMode(mode);

    mutexRelease();
}

bool SpiRtos::transmit(const uint8_t *pData, uint16_t size, bool blocking)
{
    (void) blocking;

    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    Driver::SleepGuard guard;
    startAction();
    status = finishAction(mpObj->transmit(pData, size, false));

    mutexRelease();

    return status;
}

bool SpiRtos::receive(uint8_t *pData, uint16_t size, bool blocking)
{
    (void) blocking;

    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    Driver::SleepGuard guard;
    startAction();
    status = finishAction(mpObj->receive(pData, size, false));

    mutexRelease();

    return status;
}

bool SpiRtos::transfer(uint8_t *pTxData, uint8_t *pRxData, uint16_t size,
        bool blocking)
{
    (void) blocking;

    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    Driver::SleepGuard guard;
    startAction();
    status = finishAction(mpObj->transfer(pTxData, pRxData, size, false));

    mutexRelease();

    return status;
}

bool SpiRtos::recover()
{
    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    Driver::SleepGuard guard;
    status = mpObj->recover();

    mutexRelease();

    return status;
}

// Private helpers

bool SpiRtos::mutexLock(uint32_t timeout)
{
    if (osMutexAcquire(mMutHandle, pdMS_TO_TICKS(timeout)) != osOK) {
        LOG_ERROR("Mutex timeout %u. Owner 0x%08X\n", timeout,
                osMutexGetOwner(mMutHandle));
        return false;
    }
    return true;
}

void SpiRtos::mutexRelease()
{
    osMutexRelease(mMutHandle);
}

void SpiRtos::startAction()
{
    // Drop any stale completion flag left by a previously abandoned
    // (timed-out) transfer so it cannot falsely satisfy this wait.
    osThreadFlagsClear(skCpltNotifyFlag | skErrNotifyFlag);
    mCallingThread = osThreadGetId();
}

void SpiRtos::terminateAction()
{
    mCallingThread = nullptr;
}

bool SpiRtos::finishAction(bool started)
{
    if (!started) {
        // Start was rejected (HAL_BUSY/HAL_ERROR): the peripheral is not in
        // READY state. Reset it so the next transfer can proceed.
        LOG_WARNING("Start rejected, recovering bus\n");
        terminateAction();
        mpObj->recover();
        return false;
    }

    uint32_t flags = osThreadFlagsWait(skCpltNotifyFlag | skErrNotifyFlag,
            osFlagsWaitAny, pdMS_TO_TICKS(mTimeout));

    if ((flags & osFlagsError) != 0) {
        // Timed out: no completion was signalled, so the DMA transfer is still
        // in flight and the HAL state is stuck at BUSY. Drop the pending
        // context and reset the peripheral, otherwise every later transfer
        // returns HAL_BUSY forever.
        LOG_WARNING("Transfer timed out, recovering bus\n");
        terminateAction();
        mpObj->recover();
        return false;
    }

    // A completion callback fired. On a clean error the HAL has already
    // returned the state to READY, so no recovery is needed here.
    return (flags & skErrNotifyFlag) == 0;
}

void SpiRtos::actionDone(bool status)
{
    if (mCallingThread != nullptr) {
        if (status) {
            osThreadFlagsSet(mCallingThread, skCpltNotifyFlag);
        } else {
            osThreadFlagsSet(mCallingThread, skErrNotifyFlag);
        }
    }
    mCallingThread = nullptr;
}

void SpiRtos::transferCplt(Interface::ISpi *pSpi, bool status)
{
    (void) pSpi;
    actionDone(status);
}

} // namespace Driver
