/**
 ******************************************************************************
 * @file    Flash.cpp
 * @brief   Provides access to device memory.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Flash.hpp"

#define LOG_MODULE_PRX      "Flash"
// WARNING: keep this at LOG_LEVEL_INFO. At DEBUG the power On/Off lines are
// emitted, and when the system logger is configured to persist to the eMMC each
// such line is flushed to the card - which re-powers the just-powered-off card,
// whose power-on emits yet another line: a self-sustaining ~2 s power-cycle loop
// that needlessly wears the flash. DEBUG here is bench-only, and only safe when
// the log is routed off-flash (e.g. UART/RTT).
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "System.hpp"
#include "Gpio.hpp"
#include "Mmc.hpp"

void Flash::powerModeAlwaysOn()
{
    LOG_DEBUG("AlwaysOn\n");
    enableEMMC();
    mPowerModeAlwaysOn = true;
    mPowerOffTimer.stop();
}

void Flash::powerModeAuto()
{
    mPowerModeAlwaysOn = false;
    mPowerOffTimer.start(5000);
    LOG_DEBUG("Auto\n");
}

bool Flash::init()
{
    if (mIsInit) {
        return true;
    }

    mPowerMutex.init("FlashPwr");

    pPwr = &Driver::Gpio::GetInstance(Driver::Gpio::Pin::EMMC_EN);
    pMmc = &Driver::Mmc::GetInstance(Driver::Mmc::Port::PORT_1);

    pPwr->init(Interface::IGpo::Type::PP, false, false);
    mPowerOffTimer.init("Flash", this, &Flash::powerOffHandler, false, nullptr);

    if (!enableEMMC()){
        return false;
    }

    mIsInit = mIsMmcInit;
    pMmc->setCpltHandler(this);

    if (mIsInit) {
        pMmc->blockGetSize();
        LOG_DEBUG("Initialized\n");
    }

    disableEMMC();

    return mIsInit;
}

bool Flash::deinit()
{
    if (!mIsInit) {
        return false;
    }

    pMmc->deinit();
    mIsMmcInit = false;

    mIsInit = false;

    return true;
}

uint32_t Flash::blockGetSize()
{
    if (!mIsInit) {
        return 0;
    }

    return pMmc->blockGetSize();
}

uint32_t Flash::blockGetCount()
{
    if (!mIsInit) {
        return 0;
    }

    return pMmc->blockGetCount();
}

bool Flash::blockErase(uint32_t blockIndex, bool blocking)
{
    if (!mIsInit) {
        return false;
    }

    if (!enableEMMC()) {
        return false;
    }

    bool status = pMmc->blockErase(blockIndex, blocking);

    disableEMMC();

    return status;
}

uint32_t Flash::sectorGetSize()
{
    if (!mIsInit) {
        return 0;
    }

    return pMmc->sectorGetSize();
}

uint32_t Flash::sectorGetCount()
{
    if (!mIsInit) {
        return 0;
    }

    return pMmc->sectorGetCount();
}

bool Flash::sectorErase(uint32_t sectorIndex, bool blocking)
{
    if (!mIsInit) {
        return false;
    }

    if (!enableEMMC()) {
        return false;
    }

    bool status = pMmc->sectorErase(sectorIndex, blocking);

    disableEMMC();

    return status;
}

bool Flash::memWrite(uint32_t adr, const uint8_t *pData, uint32_t size,
        bool blocking)
{
    if (!mIsInit) {
        return false;
    }

    if (!enableEMMC()) {
        return false;
    }

    bool status = pMmc->memWrite(adr, pData, size);
    if (!status) {
        mConsecErrors++;
        if (maybeRecover()) {
            status = pMmc->memWrite(adr, pData, size);
        }
    }
    if (status) {
        mConsecErrors = 0;
    }

    disableEMMC();

    return status;
}

bool Flash::memRead(uint32_t adr, uint8_t *pData, uint32_t size,
        bool blocking)
{
    if (!mIsInit) {
        return false;
    }


    if (!enableEMMC()) {
        return false;
    }

    bool status = pMmc->memRead(adr, pData, size);
    if (!status) {
        mConsecErrors++;
        if (maybeRecover()) {
            status = pMmc->memRead(adr, pData, size);
        }
    }
    if (status) {
        mConsecErrors = 0;
    }

    disableEMMC();

    return status;
}

void Flash::transferCplt(Interface::IFlash *pFlash, bool status)
{

}

bool Flash::enableEMMC()
{
    mLastAccessTimestamp = Driver::System::GetTimeMs();

    if (mPowerModeAlwaysOn) {
        return true;
    }

    // Serialize against powerOffHandler/recover so the power-on decision and the
    // mIsMmcInit flag can never be interleaved with a concurrent power-off.
    if (!mPowerMutex.lock(kPowerLockTimeoutMs)) {
        LOG_ERROR("power lock timeout\n");
        return false;
    }

    bool ok = true;
    if (!mIsMmcInit) {
        ok = powerOnInit();
        if (ok) {
            mPowerOffTimer.start(kPowerOffTimeout);
            LOG_DEBUG("On\n");
        }
    }

    mPowerMutex.release();
    return ok;
}

bool Flash::powerOnInit()
{
    // Enable power
    pPwr->set(true);

    // Give some time for the processes in EMMC
    // to stabilize before starting communication.
    // Supply power-up for 3.3V - 35ms
    // Supply power-up for 1.8V - 25ms
    OS::Delay(35);

    // Save current tread priority
    osPriority_t threadPrio = osThreadGetPriority(osThreadGetId());

    // Increase the priority to avoid getting an RX OVERRUN error
    osThreadSetPriority(osThreadGetId(), osPriorityHigh);

    // Initialize EMMC
    mIsMmcInit = pMmc->init();

    // In case of an error, try reinitializing the EMMC with a power-cycle
    uint32_t retries = 5;
    while (!mIsMmcInit && retries-- > 0) {
        LOG_WARNING("Retry\n");
        pPwr->set(false);
        OS::Delay(10);
        pPwr->set(true);
        OS::Delay(35);
        mIsMmcInit = pMmc->init();
    }

    // Restore priority
    osThreadSetPriority(osThreadGetId(), threadPrio);

    if (!mIsMmcInit) {
        pPwr->set(false);   // fallback
        LOG_ERROR("Failed to init EMMC\n");
        return false;
    }

    return true;
}

bool Flash::recover()
{
    // Serialize the whole power-cycle against enableEMMC/powerOffHandler.
    if (!mPowerMutex.lock(kPowerLockTimeoutMs)) {
        LOG_ERROR("power lock timeout\n");
        return false;
    }

    // Full power-cycle clears both a wedged SDMMC controller and a card stuck
    // holding D0 busy. deinit() first so Mmc::init() actually re-runs - it
    // early-returns while the driver's mIsInitialized is still set.
    pMmc->deinit();
    mIsMmcInit = false;

    // Drop the rail and let it collapse before re-powering.
    pPwr->set(false);
    OS::Delay(10);

    const bool ok = powerOnInit();
    if (ok) {
        mPowerOffTimer.start(kPowerOffTimeout);
    }

    mPowerMutex.release();
    LOG_WARNING("eMMC recovery %s\n", ok ? "OK" : "FAILED");
    return ok;
}

bool Flash::maybeRecover()
{
    // Recover only from a thread (never the USB MSC ISR path) and only on
    // battery: while USB is connected the mode is always-on and a host may be
    // mastering the volume, so a power-cycle must not happen. Rate-limited so a
    // permanently dead card cannot trigger a recover storm.
    if (xPortIsInsideInterrupt() != 0 || mPowerModeAlwaysOn) {
        return false;
    }

    if (mConsecErrors < kRecoverThreshold) {
        return false;
    }

    const uint32_t now = Driver::System::GetTimeMs();
    if (mLastRecoverMs != 0 && (now - mLastRecoverMs) < kRecoverCooldownMs) {
        return false;
    }
    mLastRecoverMs = now;

    LOG_WARNING("eMMC wedged (%u errors), recovering\n", mConsecErrors);
    return recover();
}

void Flash::disableEMMC()
{
    // do nothing here as we waiting poweroff timeout
}

void Flash::powerOffHandler(void *arg)
{
    if (mPowerModeAlwaysOn) {
        return;
    }

    // Runs on the shared timer daemon: never block it. If a power-on or recovery
    // is in progress (holds the lock across a delay), skip this round and retry
    // on the next tick - a fresh access will have refreshed the timestamp anyway.
    if (!mPowerMutex.lock(0)) {
        mPowerOffTimer.start(kPowerOffTimeout);
        return;
    }

    uint32_t timediff = Driver::System::GetTimeMs() - mLastAccessTimestamp;
    if (timediff < kPowerOffTimeout) {
        // Restart timer
        mPowerOffTimer.start(kPowerOffTimeout);
    } else {

        // Deinit EMMC
        pMmc->deinit();
        mIsMmcInit = false;

        // Turn off power
        pPwr->set(false);

        LOG_DEBUG("Off\n");
    }

    mPowerMutex.release();
}

