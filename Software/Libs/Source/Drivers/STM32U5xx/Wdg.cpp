/**
 ******************************************************************************
 * @file    Wdg.cpp
 * @brief   Watchdog driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Wdg"
#define LOG_MODULE_LEVEL    LOG_LEVEL_ERROR
#include "Logger.h"

#include "Wdg.hpp"

namespace Driver
{

bool Wdg::start(uint32_t ms)
{
    uint16_t reload = 0;
    uint32_t prescaler = IWDG_PRESCALER_1024;

    if (!getConfig(ms, prescaler, reload)) {
        return false;
    }

    mWdgHandle.Instance       = IWDG;
    mWdgHandle.Init.Prescaler = prescaler;
    mWdgHandle.Init.Window    = reload;   // max 4095
    mWdgHandle.Init.Reload    = reload;   // max 4095
    mWdgHandle.Init.EWI       = 1;

    bool status = (HAL_IWDG_Init(&mWdgHandle) == HAL_OK);

    if (status) {
        mIsRunning = true;
    } else {
        LOG_ERROR("HAL_IWDG_Init fail\n");
    }

    NVIC_SetPriority(IWDG_IRQn, 0);
    NVIC_EnableIRQ(IWDG_IRQn);

    return status;
}

bool Wdg::isRun()
{
    return mIsRunning;
}


bool Wdg::update(void)
{
    return HAL_IWDG_Refresh(&GetInstance().mWdgHandle) == HAL_OK;
}

bool Wdg::getConfig(uint32_t ms, uint32_t &prescaller, uint16_t &reload)
{

    // IWDG clock LSI 32kHz
    // time = (prescaler * reload ) / 32000;
    // ~2s = (16 * 4095) / 32000

    const uint32_t kReloadMax = 4095;
    const uint32_t kPrescalerMax = 1024;
    const uint32_t kPrescalerMin = 4;

    const uint32_t kPscTable[] = {
        IWDG_PRESCALER_4,
        IWDG_PRESCALER_8,
        IWDG_PRESCALER_16,
        IWDG_PRESCALER_32,
        IWDG_PRESCALER_64,
        IWDG_PRESCALER_128,
        IWDG_PRESCALER_256,
        IWDG_PRESCALER_512,
        IWDG_PRESCALER_1024,
    };

    reload = kReloadMax;
    uint16_t pscIdx = kPrescalerMax;
    uint32_t time = ((kReloadMax * kPrescalerMax * 1000) / LSI_VALUE);

    if (ms > time) {
        LOG_ERROR("Time too large: %d > %d\n", ms, time);
        return false;
    }

    do {
        for (reload = kReloadMax; reload > 0; reload--) {
            time = ((reload * pscIdx * 1000) / LSI_VALUE);
            if (ms > time) {
                goto exit;
                // use 'goto' to exit from two cycles
            }
        }
        pscIdx /= 2;
    } while (pscIdx > kPrescalerMin);

    exit :
    if (reload < kReloadMax) {
        reload++;
    }

    // get real prescaler
    for (uint8_t i = 0; i < sizeof(kPscTable) / 4; i++) {
        if (pscIdx / 4 == (1 << i)) {
            prescaller = kPscTable[i];
            break;
        }
    }

    LOG_DEBUG("pscIdx %d, reload %d, ms %d (%d)\n", pscIdx, reload, ms,
            ((reload * pscIdx * 1000) / LSI_VALUE));

    return true;
}

} /* namespace Driver */
