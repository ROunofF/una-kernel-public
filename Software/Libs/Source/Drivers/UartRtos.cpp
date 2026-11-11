/**
 ******************************************************************************
 * @file    UartRtos.cpp
 * @brief   RTOS wrapper for the UART driver that allows you to work with the
 *          driver in blocking mode, using context switching to other tasks.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "UartRtos.hpp"

#define LOG_MODULE_PRX      "UartRtos"
#define LOG_MODULE_LEVEL    LOG_LEVEL_WARNING
#include "Logger.h"

#include <cassert>

#if (USE_SYSTEM_SLEEP == 1)
#include "Sleep.hpp"
static void Pause()
{
    //Driver::Sleep::Pause();
}

static void Resume()
{
    //Driver::Sleep::Resume();
}
#else
static void Pause(){}
static void Resume(){}
#endif // (USE_SYSTEM_SLEEP == 1)

namespace Driver
{

UartRtos::UartRtos(Interface::IUart &obj, uint32_t timeout)
{
    config(obj, timeout);
}

UartRtos::~UartRtos()
{
    if (mpObj) {
        mpObj->setCpltHandler(nullptr);
    }

    if (mMutHandle) {
        osMutexDelete(mMutHandle);
    }
}

void UartRtos::config(Interface::IUart &obj, uint32_t timeout)
{
    mTimeout = timeout == 0 ? 1 : timeout;
    mpObj = &obj;
    mpObj->setCpltHandler(this);

    if (mMutHandle == nullptr) {
        const osMutexAttr_t kMutAttributes = {
                .name = "UartRtos",
                .cb_mem = &mMutControlBlock,
                .cb_size = sizeof(mMutControlBlock)
        };

        mMutHandle = osMutexNew(&kMutAttributes);
    }

}

bool UartRtos::init()
{
    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    if (mStreamHandle == nullptr) {
        mStreamHandle = xStreamBufferCreateStatic(mRxBufferSize,
                                                  1,
                                                  mpRxBuffer,
                                                  &mStreamControlBlock);
    }

    status = mpObj->init();

    mutexRelease();

    return status;
}

void UartRtos::deinit()
{
    assert(mpObj != nullptr);

    if (!mutexLock(mTimeout)) {
        return;
    }

    mpObj->deinit();

    if (mStreamHandle) {
        vStreamBufferDelete(mStreamHandle);
        mStreamHandle = nullptr;
    }

    mutexRelease();
}

bool UartRtos::startReception()
{
    assert(mpObj != nullptr);

    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    if (!mpObj->isReceptionActive()) {
        if (mpObj->startReception()) {
            Pause();
            status = true;
        }
    }

    mutexRelease();

    return status;
}

bool UartRtos::stopReception()
{
    assert(mpObj != nullptr);

    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    if (mpObj->isReceptionActive()) {
        if (mpObj->stopReception()) {
            Resume();
            status = false;
        }
    }

    mutexRelease();

    return status;
}

bool UartRtos::isReceptionActive()
{
    assert(mpObj != nullptr);

    return mpObj->isReceptionActive();
}

bool UartRtos::transmit(const uint8_t *pData, size_t size,
        uint32_t timeout)
{
    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    Pause();
    startAction();
    status = finishAction(mpObj->transmit(pData, size, 0), timeout);  // non-blocking
    Resume();

    mutexRelease();

    return status;
}

bool UartRtos::receive(uint8_t *pData, size_t size, size_t &br,
        uint32_t timeout)
{
    assert(mpObj != nullptr);

    mRxAbortFlag = false;
    br = xStreamBufferReceive(mStreamHandle, (void*) pData, size, timeout);
    if (mRxAbortFlag) {
        mRxAbortFlag = false;
        xStreamBufferReset(mStreamHandle);
        return false;
    }

    return br > 0;
}

bool UartRtos::isTxBusy()
{
    bool status = false;
    assert(mpObj != nullptr);

    if (!mutexLock(mTimeout)) {
        return status;
    }

    status = mpObj->isTxBusy();

    mutexRelease();

    return status;
}

void UartRtos::abortRx()
{
    assert(mpObj != nullptr);

    mRxAbortFlag = true;
    uint8_t dummy = 0;
    xStreamBufferSend(mStreamHandle, &dummy, 1, 1);
}

void UartRtos::flushRx()
{
    assert(mpObj != nullptr);

    if (!mutexLock(mTimeout)) {
        return;
    }

    if (mStreamHandle) {
        xStreamBufferReset(mStreamHandle);
    }

    mpObj->flushRx();

    mutexRelease();
}

size_t UartRtos::getAvailableDataSize()
{
    size_t len = 0;
    assert(mpObj != nullptr);

    if (!mutexLock(mTimeout)) {
        return len;
    }

    len = xStreamBufferBytesAvailable(mStreamHandle);

    mutexRelease();

    return len;
}

void UartRtos::setBuffer(void *pBuff, size_t size)
{
    assert(mpObj != nullptr);

    if (!mutexLock(mTimeout)) {
        return;
    }

    if (pBuff == nullptr || size == 0) {
        // Switch to internal buffer
        if (mStreamHandle) {
            vStreamBufferDelete(mStreamHandle);
            mStreamHandle = nullptr;
        }
        mpRxBuffer = mRxInternalBuff;
        mRxBufferSize = sizeof(mRxInternalBuff);

        mStreamHandle = xStreamBufferCreateStatic(mRxBufferSize,
                1, mpRxBuffer, &mStreamControlBlock);

    } else {
        // Switch to external buffer
        if (mStreamHandle) {
            vStreamBufferDelete(mStreamHandle);
            mStreamHandle = nullptr;
        }
        mStreamHandle = xStreamBufferCreateStatic(size,
                1, reinterpret_cast<uint8_t*>(pBuff), &mStreamControlBlock);

        mpRxBuffer = reinterpret_cast<uint8_t*>(pBuff);
        mRxBufferSize = size;
    }
    LOG_DEBUG("Buffer size: %u bytes\n", mRxBufferSize);

    mutexRelease();
}

bool UartRtos::setBaudrate(uint32_t baudrate)
{
    bool status = false;
    assert(mpObj != nullptr);

    if (!mutexLock(mTimeout)) {
        return status;
    }

    status = mpObj->setBaudrate(baudrate);

    mutexRelease();

    return status;
}

bool UartRtos::recover()
{
    assert(mpObj != nullptr);
    bool status = false;

    if (!mutexLock(mTimeout)) {
        return false;
    }

    status = mpObj->recover();

    mutexRelease();

    return status;
}

bool UartRtos::mutexLock(uint32_t timeout)
{
    if (osMutexAcquire(mMutHandle, pdMS_TO_TICKS(timeout)) != osOK) {
        LOG_ERROR("Mutex timeout %d. Owner 0x%08X\n", timeout,
                osMutexGetOwner(mMutHandle));
        return false;
    }
    return true;
}

void UartRtos::mutexRelease()
{
    osMutexRelease(mMutHandle);
}

void UartRtos::startAction()
{
    // Drop any stale completion flag left by a previously abandoned
    // (timed-out) transmit so it cannot falsely satisfy this wait.
    osThreadFlagsClear(skCpltNotifyFlag | skErrNotifyFlag);
    mCallingThread = osThreadGetId();
}

void UartRtos::terminateAction()
{
    mCallingThread = nullptr;
}

bool UartRtos::finishAction(bool started, uint32_t timeout)
{
    if (!started) {
        // Start was rejected (TX busy / HAL error): reset the transmitter so
        // the next transmit can proceed.
        LOG_WARNING("Tx start rejected, recovering\n");
        terminateAction();
        mpObj->recover();
        return false;
    }

    uint32_t flags = osThreadFlagsWait(skCpltNotifyFlag | skErrNotifyFlag,
            osFlagsWaitAny, pdMS_TO_TICKS(timeout));

    if ((flags & osFlagsError) != 0) {
        // Timed out: the DMA transmit never signalled completion, so mTxBusy
        // and the HAL TX state are stuck. Abort the transmit so later
        // transmits are not rejected forever.
        LOG_WARNING("Tx timed out, recovering\n");
        terminateAction();
        mpObj->recover();
        return false;
    }

    // A completion callback fired. On a clean error the HAL has already
    // returned the state to READY, so no recovery is needed here.
    return (flags & skErrNotifyFlag) == 0;
}

void UartRtos::actionDone(bool status)
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

void UartRtos::transmitCplt(Interface::IUart *pUart, bool status)
{
    actionDone(status);
}

void UartRtos::receiveCplt(Interface::IUart *pUart, size_t size)
{
    if (size > 0) {
        const size_t kSize = 128;
        uint8_t buff[kSize];
        size_t br = 0;

        int32_t cnt = 500;
        while( mpObj->receive(buff, kSize, br, 0)) {
            cnt--;
            size_t tx = xStreamBufferSendFromISR(mStreamHandle,
                    (void*)buff, br, nullptr);
            if (tx != br) {
                LOG_ERROR("Rx Buffer overflow! Current size: %u bytes\n", mRxBufferSize);
                mpObj->flushRx();
                xStreamBufferReset(mStreamHandle);
            }

            if (cnt <= 0) {
                LOG_ERROR("Cycling detected!!!  br = %u, tx = %u\n", br, tx);
                break;
            }
        }

        if (mpCpltHandler) {
            mpCpltHandler->receiveCplt(pUart, size);
        }

    }
}

} // namespace Driver
