/**
 ******************************************************************************
 * @file    Rtc.cpp
 * @brief   Real Time Clock (RTC) driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Rtc.hpp"

// LL accessors for the tick-independent WUTWF wait in setWakeUpTimer() (see the
// comment there). The __STATIC_INLINE helpers used are available without
// USE_FULL_LL_DRIVER.
#include "stm32u5xx_ll_rtc.h"

#define LOG_MODULE_PRX      "Rtc"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace Driver
{

bool Rtc::init()
{
    if (mIsInitialized) {
        return true;
    }

    mRtcHandle.Instance = RTC;
    mRtcHandle.Init.HourFormat = RTC_HOURFORMAT_24;
    mRtcHandle.Init.AsynchPrediv = 0;
    mRtcHandle.Init.SynchPrediv = 32767;
    mRtcHandle.Init.OutPut = RTC_OUTPUT_DISABLE;
    mRtcHandle.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
    mRtcHandle.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    mRtcHandle.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    mRtcHandle.Init.BinMode = RTC_BINARY_NONE;

    mRtcHandle.MspInitCallback = MspInitCallback;
    mRtcHandle.MspDeInitCallback = MspDeInitCallback;

    if (HAL_RTC_Init(&mRtcHandle) != HAL_OK) {
        LOG_ERROR("HAL_RTC_Init failed\n");
        Error_Handler();
    }

    mRtcHandle.WakeUpTimerEventCallback = WakeUpTimerEventCallback;

    RTC_PrivilegeStateTypeDef privilegeState = { 0 };
    privilegeState.rtcPrivilegeFull = RTC_PRIVILEGE_FULL_NO;
    privilegeState.backupRegisterPrivZone = RTC_PRIVILEGE_BKUP_ZONE_NONE;
    privilegeState.backupRegisterStartZone2 = RTC_BKP_DR0;
    privilegeState.backupRegisterStartZone3 = RTC_BKP_DR0;
    if (HAL_RTCEx_PrivilegeModeSet(&mRtcHandle, &privilegeState) != HAL_OK) {
        LOG_ERROR("HAL_RTCEx_PrivilegeModeSet failed\n");
        Error_Handler();
    }

    // Disable shadow registers to read subseconds
    HAL_RTCEx_EnableBypassShadow(&mRtcHandle);


    BackupValue timebkp {};
    timebkp.raw = HAL_RTCEx_BKUPRead(&mRtcHandle, skTimeBkpReg);
    if (timebkp.magic != skMagigNumber) {
        LOG_WARNING("Time not set\n");
    }

    BackupValue tzbkp {};
    tzbkp.raw = HAL_RTCEx_BKUPRead(&mRtcHandle, skTimezoneBkpReg);
    if (tzbkp.magic != skMagigNumber) {
        LOG_WARNING("Time zone not set\n");
    }


    BackupValue dstbkp {};
    dstbkp.raw = HAL_RTCEx_BKUPRead(&mRtcHandle, skDSTBkpReg);
    if (dstbkp.magic != skMagigNumber) {
        LOG_WARNING("DST not set\n");
    }

    mIsInitialized = true;

    return true;
}

void Rtc::deinit()
{
    if (!mIsInitialized) {
        return;
    }

    if (HAL_RTC_DeInit(&mRtcHandle) != HAL_OK) {
        LOG_ERROR("HAL_RTC_DeInit failed\n");
        Error_Handler();
    }
}

#if 0
uint32_t Rtc::getTimeMs()
{
    if (!mIsInitialized) {
        return 0;
    }

    RTC_TimeTypeDef time { };
    HAL_RTC_GetTime(&mRtcHandle, &time, RTC_FORMAT_BIN);

    const uint32_t min = 60;
    const uint32_t hour = min * 60;

    uint32_t ms = ((uint32_t) time.Seconds) + ((uint32_t) time.Minutes * min)
                  + ((uint32_t) time.Hours * hour);
    ms *= 1000;
    ms += (((mRtcHandle.Init.SynchPrediv + 1) - time.SubSeconds) * 1000)
            / (mRtcHandle.Init.SynchPrediv + 1);

    return ms;
}
#else   // Fast version
uint32_t Rtc::getTimeMs()
{
    if (!mIsInitialized) {
        return 0;
    }

    uint32_t subSeconds = RTC->SSR;
    uint32_t tr = RTC->TR;

    uint8_t bcdH = (tr >> 16) & 0x3F;
    uint8_t bcdM = (tr >> 8) & 0x7F;
    uint8_t bcdS = tr & 0x7F;

    uint32_t h = ((bcdH >> 4) * 10) + (bcdH & 0x0F);
    uint32_t m = ((bcdM >> 4) * 10) + (bcdM & 0x0F);
    uint32_t s = ((bcdS >> 4) * 10) + (bcdS & 0x0F);

    uint32_t totalSeconds = h * 3600 + m * 60 + s;

    uint32_t synchPrediv = mRtcHandle.Init.SynchPrediv + 1;
    uint32_t subMs = ((synchPrediv - subSeconds) * 1000) / synchPrediv;

    return totalSeconds * 1000 + subMs;
}
#endif

namespace {

// HAL_RTCEx_SetWakeUpTimer_IT() polls the WUTWF flag in a HAL_GetTick()-bounded
// loop (HAL_RTCEx_DeactivateWakeUpTimer() below just clears WUTE/WUTIE, which
// leaves WUTWF set - it does not poll). setWakeUpTimer() is called from
// the FreeRTOS tickless-idle path (Sleep.cpp vPortSuppressTicksAndSleep) with
// the SysTick suspended and interrupts masked, where HAL_GetTick() is frozen -
// so that timeout can never fire: a stuck WUTWF would spin forever with IRQs
// masked, the idle-hook watchdog feed would stop, and the IWDG would reset the
// MCU after 30 s with no capturable context. This is the sleep-side twin of the
// STOP2-wake clock-restore hang.
//
// Pre-arm the write window with an iteration-capped, tick-independent spin:
// clearing WUTE makes WUTWF settle after ~1 ck_wut + 1 RTCCLK cycle (AN4759
// Table 11, RTC3, WUCKSEL[2]=0). With the DIV16 config in use, ck_wut =
// RTCCLK/16 = 2048 Hz, so the settle is ~488 us + ~30 us ~= ~520 us - dominated
// by the ck_wut term (NOT "a couple of RTCCLK cycles"). Once WUTWF is set, the
// subsequent HAL waits exit on their first check without ever consulting
// HAL_GetTick. The limit is a finite fail-safe upper bound (not a precise
// timeout); each poll reads a volatile register so the loop is never optimised
// away.
//
// The cap's adequacy is coupled to the DIV16 / WUCKSEL[2]=0 (<=~32 s) config:
// ~520 us settle vs a ~50 ms budget at 160 MHz. A WUCKSEL[2]=1 long-period
// config (wakeup >~32 s) has a much longer WUTWF settle (up to ~1 ck_apre) that
// would exceed this cap and make waitWakeUpTimerWritable() false-time-out every
// cycle - the device would then silently never enter STOP2 (a battery
// regression, not a reboot). setWakeUpTimer() static_asserts the wakeup period
// stays in the DIV16 range so this coupling is enforced, not merely documented;
// raising it requires enlarging this cap too.
constexpr uint32_t kWutwfSpinLimit = 1000000u;

bool waitWakeUpTimerWritable()
{
    LL_RTC_DisableWriteProtection(RTC);
    LL_RTC_WAKEUP_Disable(RTC);            // WUTE = 0 -> WUTWF becomes writable
    bool writable = false;
    for (uint32_t i = 0; i < kWutwfSpinLimit; ++i) {
        if (LL_RTC_IsActiveFlag_WUTW(RTC) != 0u) {
            writable = true;
            break;
        }
    }
    LL_RTC_EnableWriteProtection(RTC);
    return writable;
}

} // namespace

bool Rtc::setWakeUpTimer(uint32_t ms)
{
    // Enforce the DIV16 / WUCKSEL[2]=0 assumption that kWutwfSpinLimit relies on
    // (see waitWakeUpTimerWritable). This mirrors the WakeUpCounter computation
    // + <=0xFFFF check below: while the max period fits the DIV16 16-bit counter
    // WUCKSEL[2] stays 0 (~520 us WUTWF settle). A longer period would force
    // WUCKSEL[2]=1, whose settle can exceed the spin cap and silently break
    // STOP2 entry - so raising skMaxSleepTimeMs past this point must also enlarge
    // kWutwfSpinLimit. (AN4759 Table 11, RTC3.)
    static_assert(static_cast<uint64_t>(LSE_VALUE) * skMaxSleepTimeMs / 16000u <= 0xFFFFu,
                  "max wakeup period overflows the DIV16 16-bit counter; a longer "
                  "period needs WUCKSEL[2]=1, whose WUTWF settle can exceed "
                  "kWutwfSpinLimit - enlarge the cap before raising skMaxSleepTimeMs");

    HAL_StatusTypeDef err = HAL_OK;

    if (ms > skMaxSleepTimeMs) {
        LOG_ERROR("Time > %d ms\n", skMaxSleepTimeMs);
        return false;
    }

    // Make the WUTWF wait tick-independent so this cannot hang the tickless-idle
    // path when HAL_GetTick() is frozen (see waitWakeUpTimerWritable()). If the
    // write window never opens, skip sleep this cycle instead of spinning until
    // the watchdog fires. Log only on the transition into the fault state: this
    // runs on every idle cycle, so an unconditional log would storm the log /
    // eMMC-flush path if WUTWF were ever permanently stuck.
    static bool sWakeUpTimerFaulted = false;
    if (!waitWakeUpTimerWritable()) {
        if (!sWakeUpTimerFaulted) {
            sWakeUpTimerFaulted = true;
            LOG_WARNING("WUTWF not ready; skipping wakeup-timer setup\n");
        }
        return false;
    }
    sWakeUpTimerFaulted = false;

    // Disable Wakeup Counter
    err = HAL_RTCEx_DeactivateWakeUpTimer(&mRtcHandle);
    if (err != HAL_OK) {
        LOG_ERROR("HAL_RTCEx_DeactivateWakeUpTimer failed.  %d\n", err);
        return false;
    }

    // Setting the Wake up time
    //  RTC Wakeup Interrupt Generation:
    // Wakeup Time Base = (RTC_WAKEUPCLOCK_RTCCLK_DIV /(LSE or LSI))
    // Wakeup Time = Wakeup Time Base * WakeUpCounter
    //   = (RTC_WAKEUPCLOCK_RTCCLK_DIV /(LSE or LSI)) * WakeUpCounter
    //     ==> WakeUpCounter = Wakeup Time / Wakeup Time Base
    //
    //  To configure the wake up timer to 4s the WakeUpCounter is set to 0x1FFF:
    // RTC_WAKEUPCLOCK_RTCCLK_DIV = RTCCLK_Div16 = 16
    // Wakeup Time Base = 16 /(32.768KHz) = 0.48828125 ms
    // Wakeup Time = 4s = 0.48828125ms  * WakeUpCounter
    //   ==> WakeUpCounter = 4s/0.48828125ms = 8192 = 0x2000

    uint32_t WakeUpCounter = (uint32_t) (LSE_VALUE * ms / 16000.0);
    if (WakeUpCounter > 0 && WakeUpCounter <= 0xFFFF) {

        err = HAL_RTCEx_SetWakeUpTimer_IT(&mRtcHandle, WakeUpCounter,
                RTC_WAKEUPCLOCK_RTCCLK_DIV16, 0);

        if (err != HAL_OK) {
            LOG_ERROR("HAL_RTCEx_SetWakeUpTimer_IT failed.  %d\n", err);
            return false;
        }
    } else {
        return false;
    }

    return true;
}

void Rtc::writeBackupReg(uint32_t reg, uint32_t value)
{
    if (reg >= RTC_BKP_DR3 && reg < RTC_BKP_NUMBER) {
        HAL_RTCEx_BKUPWrite(&mRtcHandle, reg, value);
    }
}

uint32_t Rtc::readBackupReg(uint32_t reg)
{
    if (reg >= RTC_BKP_DR3 && reg < RTC_BKP_NUMBER) {
        return HAL_RTCEx_BKUPRead(&mRtcHandle, reg);
    } else {
        return 0;
    }
}

Interface::IRtc::DateTime Rtc::get()
{
    Interface::IRtc::DateTime dt {};

    RTC_TimeTypeDef time { };
    RTC_DateTypeDef date { };

    HAL_RTC_GetTime(&mRtcHandle, &time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&mRtcHandle, &date, RTC_FORMAT_BIN);

    dt.tm_wday = (int)(date.WeekDay % 7);
    dt.tm_mon  = (int) date.Month - 1;
    dt.tm_mday = date.Date;
    dt.tm_year = 2000 + (int) date.Year - 1900;

    dt.tm_hour = time.Hours;
    dt.tm_min = time.Minutes;
    dt.tm_sec = time.Seconds;

    dt.ms = (((mRtcHandle.Init.SynchPrediv + 1) - time.SubSeconds) * 1000)
                        / (mRtcHandle.Init.SynchPrediv + 1);

    BackupValue t {};
    t.raw = HAL_RTCEx_BKUPRead(&mRtcHandle, skTimeBkpReg);
    dt.time_reliable = t.magic == skMagigNumber ? 1 : 0;

    BackupValue tz {};
    tz.raw = HAL_RTCEx_BKUPRead(&mRtcHandle, skTimezoneBkpReg);
    dt.timezone = tz.magic == skMagigNumber ? static_cast<int>(tz.value) : 0;

    BackupValue dst {};
    dst.raw = HAL_RTCEx_BKUPRead(&mRtcHandle, skDSTBkpReg);
    dt.dst_offset = dst.magic == skMagigNumber ? static_cast<int>(dst.value) : -1;

    return dt;
}

bool Rtc::set(const struct tm &dateTime)
{
    if (!mIsInitialized) {
        return false;
    }

    RTC_TimeTypeDef time = { 0 };
    RTC_DateTypeDef date = { 0 };

    HAL_StatusTypeDef err = HAL_OK;

    time.Hours = (uint8_t) dateTime.tm_hour;
    time.Minutes = (uint8_t) dateTime.tm_min;
    time.Seconds = (uint8_t) dateTime.tm_sec;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;
    err = HAL_RTC_SetTime(&mRtcHandle, &time, RTC_FORMAT_BIN);
    if (err != HAL_OK) {
        LOG_ERROR("HAL_RTC_SetTime failed. Err %d\n", err);
        return false;
    }

    int full_year = dateTime.tm_year + 1900;
    int wday      = weekdayFromYMD(full_year, dateTime.tm_mon + 1, dateTime.tm_mday);
    date.WeekDay = (uint8_t)(wday == 0 ? 7 : wday);

    date.Month   = (uint8_t) (dateTime.tm_mon + 1);
    date.Date    = (uint8_t) dateTime.tm_mday;
    date.Year    = (uint8_t) (full_year - 2000);

    err = HAL_RTC_SetDate(&mRtcHandle, &date, RTC_FORMAT_BIN);
    if (err != HAL_OK) {
        LOG_ERROR("HAL_RTC_SetDate failed. Err %d\n", err);
        return false;
    }

    BackupValue timebkp {};
    timebkp.magic = skMagigNumber;
    HAL_RTCEx_BKUPWrite(&mRtcHandle, skTimeBkpReg, timebkp.raw);

    LOG_DEBUG("time: %04u-%02u-%02u %02u:%02u:%02u\n",
                      (dateTime.tm_year + 1900),
                      (dateTime.tm_mon + 1),
                      dateTime.tm_mday,
                      dateTime.tm_hour,
                      dateTime.tm_min,
                      dateTime.tm_sec);
    return true;
}

bool Rtc::isTimeSet()
{
    BackupValue t {};
    t.raw = HAL_RTCEx_BKUPRead(&mRtcHandle, skTimeBkpReg);
    return t.magic == skMagigNumber;
}

bool Rtc::isTimeZoneSet()
{
    BackupValue tz {};
    tz.raw = HAL_RTCEx_BKUPRead(&mRtcHandle, skTimezoneBkpReg);
    return tz.magic == skMagigNumber;
}

int Rtc::getTimeZone()
{
    BackupValue tz {};
    tz.raw = HAL_RTCEx_BKUPRead(&mRtcHandle, skTimezoneBkpReg);
    return tz.magic == skMagigNumber ? static_cast<int>(tz.value) : 0;
}

void Rtc::setTimeZone(int tz)
{
    BackupValue tzbkp {};
    tzbkp.magic = skMagigNumber;
    tzbkp.value = tz;
    HAL_RTCEx_BKUPWrite(&mRtcHandle, skTimezoneBkpReg, tzbkp.raw);
    LOG_DEBUG("tz: %d\n", tz);
}

int Rtc::getDstOffset()
{
    BackupValue dst {};
    dst.raw = HAL_RTCEx_BKUPRead(&mRtcHandle, skDSTBkpReg);
    return dst.magic == skMagigNumber ? static_cast<int>(dst.value) : -1;
}

void Rtc::setDstOffset(int dstoffset)
{
    BackupValue dstbkp { };

    dstbkp.magic = skMagigNumber;
    dstbkp.value = dstoffset;
    HAL_RTCEx_BKUPWrite(&mRtcHandle, skDSTBkpReg, dstbkp.raw);
    LOG_DEBUG("dst: %d\n", dstoffset);
}

void Rtc::MspInitCallback(RTC_HandleTypeDef *rtcHandle)
{
    RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };
    if (rtcHandle->Instance == RTC) {

        // Initializes the peripherals clock
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC;
        PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
            LOG_ERROR("HAL_RCCEx_PeriphCLKConfig failed\n");
            Error_Handler();
        }

        // RTC clock enable
        __HAL_RCC_RTC_ENABLE();
        __HAL_RCC_RTCAPB_CLK_ENABLE();
        __HAL_RCC_RTCAPB_CLKAM_ENABLE();

        // RTC interrupt Init
        HAL_NVIC_SetPriority(RTC_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(RTC_IRQn);
    }
}

// Thomas/Zeller algorithm in Gregorian version
int Rtc::weekdayFromYMD(int y, int m, int d)
{
    if (m < 3) {
        m += 12;
        y -= 1;
    }

    int K   = y % 100;
    int J   = y / 100;
    int h   = (d + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7; // 0=Sat
    int dow = (h + 6) % 7; // 0=Sun .. 6=Sat (as in struct tm)

    return dow;
}

void Rtc::MspDeInitCallback(RTC_HandleTypeDef *rtcHandle)
{
    if (rtcHandle->Instance == RTC) {
        // Peripheral clock disable
        __HAL_RCC_RTC_DISABLE();
        __HAL_RCC_RTCAPB_CLK_DISABLE();
        __HAL_RCC_RTCAPB_CLKAM_DISABLE();

        // RTC interrupt Deinit
        HAL_NVIC_DisableIRQ(RTC_IRQn);
    }
}

void Rtc::IrqHandler()
{
    HAL_RTCEx_WakeUpTimerIRQHandler(&GetInstance().mRtcHandle);
}

void Rtc::WakeUpTimerEventCallback(RTC_HandleTypeDef *rtcHandle)
{
    // Repeat prevention
    HAL_RTCEx_DeactivateWakeUpTimer(rtcHandle);
}

} /* namespace Driver */
