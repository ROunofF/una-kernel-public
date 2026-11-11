/**
 ******************************************************************************
 * @file    System.cpp
 * @brief   System low level functionality.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include <cassert>

#include "System.hpp"
#include "MD5/md5.hpp"

// LL accessors for the STOP2 wake clock restore (register-level ready checks
// with no internal HAL_GetTick timeout; the __STATIC_INLINE helpers used here
// are available without USE_FULL_LL_DRIVER).
#include "stm32u5xx_ll_rcc.h"

#define LOG_MODULE_PRX      "System"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

namespace Driver
{

// Static fields
System::ResetReason System::mReserReason;
uint32_t System::mResetFlagsRaw;
uint32_t System::mCounterHSE;

void System::Init()
{
    static bool sIsInitialized;

    if (sIsInitialized) {
        return;
    }

    // Latch the raw reset-flag word before anything clears it, so the crash
    // tooling can decode the precise cause (the coarse enum below collapses
    // brown-out / pin / power-on into HARDWARE and has no WWDG distinction).
    mResetFlagsRaw = RCC->CSR;

    // Get reset reason
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET) {
        mReserReason = ResetReason::SOFTWARE;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) {
        mReserReason = ResetReason::WATCHDOG;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != RESET) {
        mReserReason = ResetReason::STANDBY;
    } else {
        mReserReason = ResetReason::HARDWARE;
    }

    // Configure LSE Drive Capability
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

    // Configure the system clock
    ClockConfig160Mhz();

    sIsInitialized = true;
}

void System::ClearResetFlags()
{
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

void System::LogResetInfo()
{
    const uint32_t csr = mResetFlagsRaw;

    LOG_WARNING("Reset reason: %s (RCC_CSR=0x%08lX)\n",
                DRV_SYSTEM_RESET_TO_STR(mReserReason), (unsigned long)csr);

    // Decode every flag that is set - more than one can be latched at once,
    // and the coarse reason above only reports the highest-priority one.
    if (csr & RCC_CSR_LPWRRSTF) { LOG_WARNING("  flag: LPWRRST (low-power / illegal Stop-Standby entry)\n"); }
    if (csr & RCC_CSR_WWDGRSTF) { LOG_WARNING("  flag: WWDGRST (window watchdog)\n"); }
    if (csr & RCC_CSR_IWDGRSTF) { LOG_WARNING("  flag: IWDGRST (independent watchdog)\n"); }
    if (csr & RCC_CSR_SFTRSTF)  { LOG_WARNING("  flag: SFTRST  (software / NVIC_SystemReset)\n"); }
    if (csr & RCC_CSR_BORRSTF)  { LOG_WARNING("  flag: BORRST  (brownout or power-on)\n"); }
    if (csr & RCC_CSR_PINRSTF)  { LOG_WARNING("  flag: PINRST  (NRST pin)\n"); }
    if (csr & RCC_CSR_OBLRSTF)  { LOG_WARNING("  flag: OBLRST  (option byte loader)\n"); }
}

uint32_t System::GetTimeMs()
{
    return HAL_TICKS_TO_MS(HAL_GetTick());
}

void System::Delay(uint32_t ms)
{
    HAL_Delay(HAL_MS_TO_TICKS(ms));
}

#if (USE_SYSTEM_SLEEP == 1)

namespace {

// Coarse, tick-independent spin budget for the STOP2 wake clock restore. Each
// poll reads a volatile RCC/FLASH register, so the loop is never optimised
// away. The count is an upper bound only (~hundreds of ms at MSIS speed) and
// merely needs to be finite - it is a fail-safe, not a precise timeout.
constexpr uint32_t kWakeSpinLimit = 4000000u;

// Poll `ready` up to kWakeSpinLimit times; true if it became set, false on timeout.
template <typename Fn>
bool spinUntilReady(Fn ready)
{
    for (uint32_t i = 0; i < kWakeSpinLimit; ++i) {
        if (ready()) {
            return true;
        }
    }
    return ready();
}

} // namespace

void System::Sleep()
{
    // Stop2
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
}

void System::WakeRestoreClock()
{
    // Runs with IRQs masked and SysTick suspended (HAL_GetTick frozen). Every
    // wait below is bounded by spinUntilReady() rather than a HAL_GetTick
    // timeout, so a stuck oscillator/PLL can no longer hang the idle task until
    // the watchdog fires. On a timed-out wait: in a debug build assert() captures
    // a full crash dump (its synchronous udf fault is not masked by PRIMASK,
    // unlike the IWDG early interrupt - reason ASSERT, exact file/line, stacks +
    // CFI backtrace); under NDEBUG assert() is a no-op, so the following
    // System::Reset() recovers the device without a dump. Either way it never
    // spins. Only what STOP2 tears down is restored; LSE/LSI/MSIK and the RCC
    // config registers are retained.

    // 1) Restore voltage scale 1 and re-enable the EPOD boost STOP2 dropped.
    //    HAL bounds this internally with a decrementing counter (not
    //    HAL_GetTick), so it is safe here and cannot hang.
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    // 2) Raise flash wait states for 160 MHz before the core speeds up.
    __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_4);
    if (!spinUntilReady([] { return __HAL_FLASH_GET_LATENCY() == FLASH_LATENCY_4; })) {
        assert(false && "wake: flash latency");  // debug: capture dump
        System::Reset();                         // NDEBUG: recover without a dump
    }

    // 3) MSIS is the PLL source and the post-STOP2 system clock, but STOP2
    //    restarts it at its reduced stop range (~24 MHz), not the 48 MHz run
    //    range. Left unrestored, the PLL would re-lock at half the VCO and leave
    //    the whole chip at 80 MHz while SystemCoreClock wrongly reports 160.
    //    Wait for MSIS, restore RANGE_0 (48 MHz), and let it settle before the
    //    PLL re-locks. MSIRGSEL stays software-selected across STOP2, so writing
    //    ICSCR1.MSISRANGE is sufficient.
    if (!spinUntilReady([] { return LL_RCC_MSIS_IsReady() != 0u; })) {
        assert(false && "wake: MSIS ready");
        System::Reset();
    }

    LL_RCC_MSIS_SetRange(LL_RCC_MSISRANGE_0);
    if (!spinUntilReady([] { return LL_RCC_MSIS_IsReady() != 0u; })) {
        assert(false && "wake: MSIS range restore");
        System::Reset();
    }

    // 4) Re-enable PLL1 (M/N/R config retained) and wait for lock.
    LL_RCC_PLL1_Enable();
    if (!spinUntilReady([] { return LL_RCC_PLL1_IsReady() != 0u; })) {
        assert(false && "wake: PLL1 lock");
        System::Reset();
    }

    // 5) Switch SYSCLK back to PLL1 and confirm the switch took effect.
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL1);
    if (!spinUntilReady([] {
            return LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_PLL1;
        })) {
        assert(false && "wake: SYSCLK switch");
        System::Reset();
    }

    // Core is back at 160 MHz. Recompute SystemCoreClock from the actual
    // registers instead of hardcoding it, so it can never silently disagree
    // with the real clock (which is exactly how the halved-clock bug hid).
    SystemCoreClockUpdate();
}

void System::Wake()
{
    bool wasSet = __HAL_PWR_GET_FLAG(PWR_FLAG_STOPF);

    // Clear Wake Up Flag
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_STOPF);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SBF);

    if (wasSet) {
        // Bounded, LL-based restore (was the full HAL ClockConfig160Mhz, whose
        // HAL_GetTick timeouts are defeated while the tick is suspended).
        WakeRestoreClock();
    }
}
#endif // (USE_SYSTEM_SLEEP == 1)

void System::Reset()
{
    NVIC_SystemReset();
}

const System::McuUid_t& System::GetMcuUid()
{
    McuUid_t *pUid = (McuUid_t*) UID_BASE;
    return *pUid;
}

const char* System::GetMcuUidStr()
{
    static char uidstr[(sizeof(System::McuUid_t) * 2) + 1];

    if (uidstr[0] == '\0') {
        const McuUid_t& uuid = GetMcuUid();
        for (uint8_t i = 0; i < sizeof(System::McuUid_t); i++) {
            sprintf(&uidstr[i*2], "%02X", uuid.bytes[i]);
        }
    }
    return uidstr;
}

const char* System::GetDeviceIdStr()
{
    static char devidstr[7] = {0};

    if (devidstr[0] == '\0') {
        MD5 md5;
        md5.update(GetMcuUid().bytes, sizeof(System::McuUid_t::bytes));
        auto digest = md5.finalize();

        uint32_t id = 2166136261;           // FNV-1a
        for (size_t idx = 0; idx < sizeof(digest.bytes); ++idx) {
            id ^= digest.bytes[idx];
            id *= 16777619u;
        }

        id %= 1000000u;

        sprintf(devidstr, "%06lu", id);
    }

    return devidstr;
}

const char* System::GetManufacturerNameStr()
{
    return "UNA";
}

const char* System::GetModelNameStr()
{
    return "UNA Watch";
}

const char* System::GetBuildVersionStr()
{
    return BUILD_VERSION;
}

const char* System::GetProjectName()
{
    return PROJECT_NAME;
}

bool System::RequestHSE(bool enable)
{
    RCC_OscInitTypeDef RCC_OscInitStruct { };
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;

    if (enable) {
        if (mCounterHSE == 0) {
            RCC_OscInitStruct.HSEState = RCC_HSE_ON;
            if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
                return false;
            }
        }
        if (mCounterHSE < UINT32_MAX) {
            mCounterHSE++;
        }
    } else {
        if (mCounterHSE > 0) {
            mCounterHSE--;
        }

        if (mCounterHSE == 0) {
            RCC_OscInitStruct.HSEState = RCC_HSE_OFF;
            if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
                return false;
            }
        }
    }

    return true;
}

SDK::FirmwareVersion System::GetLibcVersion()
{
    FwInfoBlock_t *libc = reinterpret_cast<FwInfoBlock_t*>(LIBC_INFO_BLOCK_ADDR);
    SDK::FirmwareVersion ver = SDK::ParseVersion(libc->version + 3); // skip 'sl.'
    return ver;
}

const char* System::GetLibcVersionStr()
{
    FwInfoBlock_t *libc = reinterpret_cast<FwInfoBlock_t*>(LIBC_INFO_BLOCK_ADDR);
    return libc->version;
}

SDK::FirmwareVersion System::GetBootVersion()
{
    FwInfoBlock_t* boot = reinterpret_cast<FwInfoBlock_t*>(BOOT_INFO_BLOCK_ADDR);
    SDK::FirmwareVersion ver = SDK::ParseVersion(boot->version + 3); // skip 'bl.'
    return ver;
}

const char* System::GetBootVersionStr()
{
    FwInfoBlock_t *boot = reinterpret_cast<FwInfoBlock_t*>(BOOT_INFO_BLOCK_ADDR);
    return boot->version;
}

void System::ClockConfig160Mhz()
{
    // Configure the main internal regulator output voltage for 160MHz range
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    // Initializes the CPU, AHB and APB buses clocks
    RCC_OscInitTypeDef RCC_OscInitStruct { };
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI | // Low power timers for LCD
                                       RCC_OSCILLATORTYPE_LSE;  // RTC
    RCC_OscInitStruct.LSIState = RCC_LSI_ON;
    RCC_OscInitStruct.LSIDiv = RCC_LSI_DIV1;
    RCC_OscInitStruct.LSEState = CLK_32K_SRC_TYPE;

    // MSIK oscillator for LPUART1
    RCC_OscInitStruct.OscillatorType |= RCC_OSCILLATORTYPE_MSIK;
    RCC_OscInitStruct.MSIKState = RCC_MSIK_ON;
    RCC_OscInitStruct.MSIKClockRange = RCC_MSIKRANGE_4;  // 4 MHz


#if 1   // MSIS 48 MHz
    RCC_OscInitStruct.OscillatorType |= RCC_OSCILLATORTYPE_MSI; // Everything else
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_0;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;

    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
    RCC_OscInitStruct.PLL.PLLM = 3;
    RCC_OscInitStruct.PLL.PLLN = 10;
    RCC_OscInitStruct.PLL.PLLP = 1; // Not used
    RCC_OscInitStruct.PLL.PLLQ = 2; // MMC: (48 / 3) * 10 / 2 = 80 MHz
    RCC_OscInitStruct.PLL.PLLR = 1; // Out: (48 / 3) * 10 / 1 = 160 MHz
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_1;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
#endif

#if 0   // HSI 16 MHz
    RCC_OscInitStruct.OscillatorType |= RCC_OSCILLATORTYPE_HSI; // Everything else
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;

    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 20;
    RCC_OscInitStruct.PLL.PLLP = 1; // Not used
    RCC_OscInitStruct.PLL.PLLQ = 4; // MMC: 16 * 20 / 4 = 80 MHz
    RCC_OscInitStruct.PLL.PLLR = 2; // Out: 16 * 20 / 10 = 160 MHz
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_1;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
#endif

#if 0   // HSE 16 MHz
    RCC_OscInitStruct.OscillatorType |= RCC_OSCILLATORTYPE_HSE; // Everything else
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;

    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 20;
    RCC_OscInitStruct.PLL.PLLP = 1; // Not used
    RCC_OscInitStruct.PLL.PLLQ = 4; // MMC: 16 * 20 / 4 = 80 MHz
    RCC_OscInitStruct.PLL.PLLR = 2; // Out: 16 * 20 / 10 = 160 MHz
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_1;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
#endif


    if (mCounterHSE > 0) {
        RCC_OscInitStruct.OscillatorType |= RCC_OSCILLATORTYPE_HSE;
        RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    }

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    // Initializes the CPU, AHB and APB buses clocks
    RCC_ClkInitTypeDef RCC_ClkInitStruct { };
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 |
                                  RCC_CLOCKTYPE_PCLK2 |
                                  RCC_CLOCKTYPE_PCLK3;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler();
    }
}

} /* namespace Driver */
