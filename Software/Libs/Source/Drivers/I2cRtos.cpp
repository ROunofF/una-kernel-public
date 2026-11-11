/**
 ******************************************************************************
 * @file    I2cRtos.cpp
 * @brief   Thread-safe wrapper for I2C driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "I2cRtos.hpp"

#define LOG_MODULE_PRX      "I2cRtos"
#define LOG_MODULE_LEVEL    LOG_LEVEL_WARNING
#include "Logger.h"

#include <cassert>
#include "main.h"


#include "Sleep.hpp"

namespace Driver
{

I2cRtos::I2cRtos(Interface::II2c *pObj, uint32_t timeout)
{
    assert(pObj != nullptr);
    config(pObj, timeout);
}

I2cRtos::~I2cRtos()
{
    if (mpObj) {
        mpObj->setCpltHandler(nullptr);
    }

    if (mMutHandle) {
        osMutexDelete(mMutHandle);
    }
}

void I2cRtos::config(Interface::II2c *pObj, uint32_t timeout)
{
    assert(pObj != nullptr);

    mTimeout = timeout == 0 ? 1 : timeout;
    mpObj = pObj;
    mpObj->setCpltHandler(this);

    if (mMutHandle == nullptr) {
        const osMutexAttr_t kMutAttributes = {
                .name = "I2cRtos",
                .attr_bits = 0,
                .cb_mem = &mMutControlBlock,
                .cb_size = sizeof(mMutControlBlock)
        };

        mMutHandle = osMutexNew(&kMutAttributes);
    }
}

bool I2cRtos::init()
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

void I2cRtos::deinit()
{
    assert(mpObj != nullptr);

    if (!mutexLock(mTimeout)) {
        return;
    }

    Driver::SleepGuard guard;
    mpObj->deinit();

    mutexRelease();
}

bool I2cRtos::transmit(uint8_t devAddr, const uint8_t *pData, uint16_t size,
        bool blocking)
{
    (void) blocking;

    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    Driver::SleepGuard guard;
    waitBusFree();
    startAction();
    status = finishAction(mpObj->transmit(devAddr, pData, size, false));

    mutexRelease();

    return status;
}

bool I2cRtos::receive(uint8_t devAddr, uint8_t *pData, uint16_t size,
        bool blocking)
{
    (void) blocking;

    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    Driver::SleepGuard guard;
    waitBusFree();
    startAction();
    status = finishAction(mpObj->receive(devAddr, pData, size, false));


    mutexRelease();

    return status;
}

bool I2cRtos::writeMem(uint8_t devAddr, uint16_t memAddr, uint8_t memAddrSize,
        const uint8_t *pData, uint16_t size, bool blocking)
{
    (void) blocking;

    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    Driver::SleepGuard guard;
    waitBusFree();
    startAction();
    status = finishAction(mpObj->writeMem(devAddr, memAddr, memAddrSize, pData, size, false));

    mutexRelease();

    return status;
}

bool I2cRtos::readMem(uint8_t devAddr, uint16_t memAddr, uint8_t memAddrSize,
        uint8_t *pData, uint16_t size, bool blocking)
{
    (void) blocking;

    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    Driver::SleepGuard guard;
    waitBusFree();
    startAction();
    status = finishAction(mpObj->readMem(devAddr, memAddr, memAddrSize, pData, size, false));

    mutexRelease();

    return status;
}

bool I2cRtos::isDeviceReady(uint8_t devAddr)
{
    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    Driver::SleepGuard guard;
    waitBusFree();
    status = mpObj->isDeviceReady(devAddr);

    mutexRelease();

    return status;
}

bool I2cRtos::isBusBusy()
{
    assert(mpObj != nullptr);
    return mpObj->isBusBusy();
}

bool I2cRtos::recover()
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

void I2cRtos::setBusFreeWait(uint32_t maxWaitMs, uint32_t stepMs,
                             uint32_t graceMs)
{
    mBusFreeMaxWaitMs = maxWaitMs;
    mBusFreeStepMs    = stepMs == 0 ? 1 : stepMs;
    mBusFreeGraceMs   = graceMs;
}

void I2cRtos::waitBusFree()
{
    if (mBusFreeMaxWaitMs == 0 || mpObj == nullptr) {
        return;
    }

    // Common path: bus already idle. Return immediately, no overhead.
    if (!mpObj->isBusBusy()) {
        return;
    }

    uint32_t waited = 0;
    while (mpObj->isBusBusy() && waited < mBusFreeMaxWaitMs) {
        osDelay(pdMS_TO_TICKS(mBusFreeStepMs));
        waited += mBusFreeStepMs;
    }

    if (waited >= mBusFreeMaxWaitMs) {
        // The bus never freed on its own - a slave is most likely holding SDA
        // low after an interrupted transfer. Drive a bus-level recovery now
        // instead of letting the imminent start fail first.
        LOG_WARNING("Bus stuck busy after %u ms, recovering\n", waited);
        mpObj->recover();
        return;
    }

    // BUSY just cleared, but a dying slave may briefly re-pull SDA right
    // after release. A short settling window prevents the next transaction
    // from racing into that gap.
    if (mBusFreeGraceMs > 0) {
        osDelay(pdMS_TO_TICKS(mBusFreeGraceMs));
    }

    LOG_DEBUG("Bus released after %u ms (+%u ms grace)\n",
              waited, mBusFreeGraceMs);
}

bool I2cRtos::mutexLock(uint32_t timeout)
{
    if (osMutexAcquire(mMutHandle, pdMS_TO_TICKS(timeout)) != osOK) {
        LOG_ERROR("Mutex timeout %u. Owner 0x%08X\n", timeout,
                osMutexGetOwner(mMutHandle));
        return false;
    }
    return true;
}

void I2cRtos::mutexRelease()
{
    osMutexRelease(mMutHandle);
}

void I2cRtos::startAction()
{
    // Drop any stale completion flag left by a previously abandoned
    // (timed-out) transfer so it cannot falsely satisfy this wait.
    osThreadFlagsClear(skCpltNotifyFlag | skErrNotifyFlag);
    mCallingThread = osThreadGetId();
}

void I2cRtos::terminateAction()
{
    mCallingThread = nullptr;
}

bool I2cRtos::finishAction(bool started)
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
        // Timed out: no completion was signalled, so the transfer is still in
        // flight and hi2c->State is stuck at BUSY. Drop the pending context
        // and reset the peripheral, otherwise every later transfer returns
        // HAL_BUSY forever.
        LOG_WARNING("Transfer timed out, recovering bus\n");
        terminateAction();
        mpObj->recover();
        return false;
    }

    // A completion callback fired. On a clean error (e.g. a NACK, which is
    // normal for some devices) the HAL has already returned the state to
    // READY, so no recovery is needed here.
    return (flags & skErrNotifyFlag) == 0;
}

void I2cRtos::actionDone(bool status)
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

void I2cRtos::transferCplt(Interface::II2c *pI2c, bool status)
{
    actionDone(status);
}

} // namespace Driver
