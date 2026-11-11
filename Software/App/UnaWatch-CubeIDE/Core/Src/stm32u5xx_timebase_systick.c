/**
  ******************************************************************************
  * @file    stm32u5xx_timebase_systick.c
  * @brief   HAL/RTOS time base based on the hardware SysTick (LSE clocked).
  ******************************************************************************
  *
  ******************************************************************************
  */

#include "stm32u5xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

// Reference timer to restore system ticks
LPTIM_HandleTypeDef hlptim4;
static const uint32_t skRefTimOverflow = 65536;
static uint32_t sRefTimOverflowCnt = 0;
static uint32_t sRefTimPendingTicks = 0;
static uint16_t sRefTimPrevTicks = 0;

uint32_t HAL_GetTicksBehindReference(void);

/* Reads the reference counter. It runs on the LSE, asynchronous to the bus, so
 * a single read can catch it mid-update: a reading counts only when two in a
 * row agree. */
static uint16_t RefTimReadCounter(void)
{
    uint16_t ticks = (uint16_t)HAL_LPTIM_ReadCounter(&hlptim4);

    for (uint32_t i = 0; i < 8U; i++) {
        uint16_t again = (uint16_t)HAL_LPTIM_ReadCounter(&hlptim4);
        if (again == ticks) {
            break;
        }
        ticks = again;
    }

    return ticks;
}

/* Reads the reference counter and counts a wrap since the last look; returns
 * the reading. Must be looked at at least once per period (64 s). */
static uint16_t RefTimTrackWrap(void)
{
    uint16_t ticks = RefTimReadCounter();
    if (sRefTimPrevTicks > ticks) {
        sRefTimOverflowCnt++;
    }
    sRefTimPrevTicks = ticks;

    return ticks;
}

static uint32_t RefTimAbsolute(uint16_t ticks)
{
    return sRefTimPendingTicks + ticks + (sRefTimOverflowCnt * skRefTimOverflow);
}


/* Renamed default implementation with 1024 Hz frequency */
HAL_StatusTypeDef HAL_InitTick_default(uint32_t TickPriority)
{
    uint32_t ticknumber = 0U;
    uint32_t systicksel;

    /* Check uwTickFreq for MisraC 2012 */
    if ((uint32_t)uwTickFreq == 0UL)
    {
        return HAL_ERROR;
    }

    /* Check Clock source to calculate the tickNumber */
    if (READ_BIT(SysTick->CTRL, SysTick_CTRL_CLKSOURCE_Msk) == SysTick_CTRL_CLKSOURCE_Msk)
    {
        /* HCLK selected as SysTick clock source */
        /* Modified for 1024 Hz instead of 1000 Hz */
        ticknumber = SystemCoreClock / 1024UL;
    }
    else
    {
        systicksel = HAL_SYSTICK_GetCLKSourceConfig();
        switch (systicksel)
        {
            /* HCLK_DIV8 selected as SysTick clock source */
            case SYSTICK_CLKSOURCE_HCLK_DIV8:
                ticknumber = (SystemCoreClock / 8192UL);  /* 8 * 1024 */
                break;
            /* LSI selected as SysTick clock source */
            case SYSTICK_CLKSOURCE_LSI:
                ticknumber = (LSI_VALUE / 1024UL);
                break;
            /* LSE selected as SysTick clock source */
            case SYSTICK_CLKSOURCE_LSE:
                ticknumber = (LSE_VALUE / 1024UL);  /* 32768 / 1024 = 32 */
                break;
            default:
                /* Nothing to do */
                break;
        }
    }

    /* Configure the SysTick to have interrupt at 1024 Hz */
    if (HAL_SYSTICK_Config(ticknumber) > 0U)
    {
        return HAL_ERROR;
    }

    /* Configure the SysTick IRQ priority */
    HAL_NVIC_SetPriority(SysTick_IRQn, TickPriority, 0U);
    uwTickPrio = TickPriority;

    /* Return function status */
    return HAL_OK;
}

/* Custom HAL_InitTick that tries to use LSE if available */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    /* If FreeRTOS is used, verify correct priority is requested */
#ifdef configLIBRARY_LOWEST_INTERRUPT_PRIORITY
    if (TickPriority != configLIBRARY_LOWEST_INTERRUPT_PRIORITY) {
        /* When using FreeRTOS, tick priority must match kernel priority */
        configASSERT(0);
        TickPriority = configLIBRARY_LOWEST_INTERRUPT_PRIORITY;
    }
#endif

    /* Check if LSE is ready and SysTick is already configured for LSE */
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY))
    {
        uint32_t current_source = HAL_SYSTICK_GetCLKSourceConfig();

        /* If SysTick is already on LSE with correct settings, do nothing */
        if ((current_source == SYSTICK_CLKSOURCE_LSE) &&
            (SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) &&
            (SysTick->LOAD == 31U))  /* 32768/1024 - 1 = 31 */
        {
            return HAL_OK;
        }

        /* LSE is ready but SysTick not configured - try to configure for LSE */
        SysTick->CTRL = 0UL;  /* Disable during configuration */

        /* Select LSE as SysTick clock source */
        HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_LSE);

        /* Configure SysTick for 1024 Hz from 32768 Hz LSE
         * Reload value = (32768 / 1024) - 1 = 31
         */
        uint32_t ticknumber = 32U;  /* HAL_SYSTICK_Config subtracts 1 internally */

        /* Configure the SysTick to have interrupt frequency 1024 Hz */
        if (HAL_SYSTICK_Config(ticknumber) > 0U)
        {
            /* Failed to configure with LSE - fallback to default */
            return HAL_InitTick_default(TickPriority);
        }

        /* Configure the SysTick IRQ priority */
        HAL_NVIC_SetPriority(SysTick_IRQn, TickPriority, 0U);
        uwTickPrio = TickPriority;

        return HAL_OK;
    }
    else
    {
        /* LSE not ready - use default configuration (HCLK) */
        return HAL_InitTick_default(TickPriority);
    }
}

/**
 * @brief   This function handles System tick timer.
 * @note    To synchronize time between HAL and RTOS using sleep mode,
 *          we use SysTick as the only time source.
 */

void SysTick_HandlerInternal(void)
{
    /* Increment HAL ticks */
    HAL_IncTick();

    /* Increment FreeRTOS ticks */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        /* Save interrupt mask and disable interrupts */
        uint32_t ulPreviousMask = portSET_INTERRUPT_MASK_FROM_ISR();
        {
            /* Increment the RTOS tick */
            if (xTaskIncrementTick() != pdFALSE) {
                /* Pend a context switch */
                portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;
            }
        }
        /* Restore interrupt mask */
        portCLEAR_INTERRUPT_MASK_FROM_ISR(ulPreviousMask);
    }

    /* Check reference timer wraparound */
    if (hlptim4.Instance == LPTIM4) {   // Check if timer initialized
        RefTimTrackWrap();
    } else {
        // Just save the ticks to align them with the HAL ticks later
        sRefTimPendingTicks++;
    }
}

/**
 * @brief   Brings the HAL and RTOS ticks up to the reference timer, short of it
 *          by `lead` ticks, one tick at a time.
 * @note    Interrupts masked or in the SysTick handler. With the scheduler
 *          suspended the RTOS ticks are pended and run on resume.
 */
static void TickCatchUp(uint32_t lead)
{
    uint32_t behind = HAL_GetTicksBehindReference();
    if (behind > lead) {
        uint32_t catchUp = behind - lead;
        for (uint32_t i = 0; i < catchUp; i++) {
            SysTick_HandlerInternal();
        }
    }
}

/**
 * @brief   How many ticks the HAL tick is behind the reference timer; 0 when
 *          it is not behind.
 * @note    Interrupts masked, or in the SysTick handler itself: it counts
 *          wraps into state the SysTick handler also counts into, with a
 *          read-compare-store that a higher-priority interrupt could split.
 */
uint32_t HAL_GetTicksBehindReference(void)
{
    configASSERT((__get_PRIMASK() != 0U) || (__get_IPSR() == 15U));   // 15 = SysTick exception

    if (hlptim4.Instance != LPTIM4) {   // Check if timer initialized
        return 0;
    }

    // A wrap since the last look is counted before the absolute time is
    // taken, or it reads a whole period short and nothing is behind. Both come
    // from the one reading.
    uint32_t absoluteTicks = RefTimAbsolute(RefTimTrackWrap());

    // Both counts wrap at 2^32 and stay within a few ticks of each other, so
    // the difference is taken modulo 2^32 and read as signed.
    int32_t behind = (int32_t)(absoluteTicks - uwTick);

    return (behind > 0) ? (uint32_t)behind : 0;
}

void HAL_CatchUpTicks(void)
{
    TickCatchUp(0);
}

// 'SysTick_Handler' is redefined in FreeRTOSConfig.h and used in port.c
// To define the real 'SysTick_Handler' here, use #undef
#ifdef SysTick_Handler
#undef SysTick_Handler
#endif
/**
 * @brief   This function handles System tick timer.
 * @note    To synchronize time between HAL and RTOS using sleep mode,
 *          we use SysTick as the only time source.
 */
void SysTick_Handler(void)
{
    // Tick correction.
    // If HAL and FreeRTOS ticks lag behind real time due to drift caused by
    // the start/stop of the SysTick timer, they are caught up here; the call
    // below is this interrupt's own tick.
    TickCatchUp(1);

    SysTick_HandlerInternal();
}

/**
 * @brief  Override weak vPortSetupTimerInterrupt from FreeRTOS port.c
 *         SysTick is already configured by HAL_InitTick()
 */
void vPortSetupTimerInterrupt(void)
{
    /* SysTick already configured by HAL_InitTick() before scheduler starts
     * Just verify configuration is correct
     */

    /* Verify SysTick reload value is correct for 1024 Hz from 32768 Hz */
    configASSERT(SysTick->LOAD == 31UL);

    /* Verify priority */
    uint32_t ulCurrentPriority = NVIC_GetPriority(SysTick_IRQn);
    configASSERT(ulCurrentPriority == configLIBRARY_LOWEST_INTERRUPT_PRIORITY);

    /* If priority is wrong, fix it */
    if (ulCurrentPriority != configLIBRARY_LOWEST_INTERRUPT_PRIORITY) {
        HAL_NVIC_SetPriority(SysTick_IRQn, configLIBRARY_LOWEST_INTERRUPT_PRIORITY, 0);
    }

    /* SysTick is already enabled and running */
}


/**
 * @brief   Initialize an additional reference timer that has the
 *          same clock source (LSE) and tick rate (1024Hz) as SysTick to
 *          restore the tick count after exiting STOP2. We use LPTIM4 which
 *          does not turn off during STOP2 mode.
 * @retval  HAL status
 */
HAL_StatusTypeDef HAL_InitReferenceTim()
{
    /* Enable autonomous mode for STOP2 */
    __HAL_RCC_LPTIM4_CLKAM_ENABLE();

    /* Enable LPTIM4 clock */
    __HAL_RCC_LPTIM4_CLK_ENABLE();

    /* Configure LPTIM4 clock source to LSE */
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_LPTIM34;
    PeriphClkInit.Lptim34ClockSelection = RCC_LPTIM34CLKSOURCE_LSE;

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
        return HAL_ERROR;
    }

    /* Initialize LPTIM4 peripheral  */
    hlptim4.Instance = LPTIM4;
    hlptim4.Init.Clock.Source = LPTIM_CLOCKSOURCE_APBCLOCK_LPOSC;
    hlptim4.Init.Clock.Prescaler = LPTIM_PRESCALER_DIV32;
    hlptim4.Init.Trigger.Source = LPTIM_TRIGSOURCE_SOFTWARE;
    hlptim4.Init.Period = skRefTimOverflow - 1; // ~63.9s
    hlptim4.Init.UpdateMode = LPTIM_UPDATE_IMMEDIATE;
    hlptim4.Init.CounterSource = LPTIM_COUNTERSOURCE_INTERNAL;
    hlptim4.Init.Input1Source = LPTIM_INPUT1SOURCE_GPIO;
    hlptim4.Init.Input2Source = LPTIM_INPUT2SOURCE_GPIO;
    hlptim4.Init.RepetitionCounter = 0;

    if (HAL_LPTIM_Init(&hlptim4) != HAL_OK) {
        return HAL_ERROR;
    }

    __HAL_LPTIM_RESET_COUNTER(&hlptim4);

    /* Start LPTIM4 in continuous mode */
    if (HAL_LPTIM_Counter_Start(&hlptim4) != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

uint16_t HAL_GetReferenceTicks()
{
    if (hlptim4.Instance == LPTIM4) {   // Check if timer initialized
        return RefTimReadCounter();
    } else {
        return uwTick;  // Synchronously with SysTick
    }
}

uint16_t HAL_GetReferenceTicksPassed(uint16_t FromTicks)
{
    return (uint16_t)(HAL_GetReferenceTicks() - FromTicks);
}

/* The reference time in ticks, without counting a wrap first: in the tick
 * after a wrap it can read a whole period (64 s) short. For statistics only;
 * the tick is caught up through HAL_GetTicksBehindReference(). */
uint32_t HAL_GetReferenceAbsoluteTicks()
{
    if (hlptim4.Instance == LPTIM4) {   // Check if timer initialized
        return RefTimAbsolute(RefTimReadCounter());
    } else {
        return uwTick;  // Synchronously with SysTick
    }
}

#if 0
// TODO: MCU does not wake up from LPTIM4 interrupt.
// Fix this and replace RTC with LPTIM4 to wake up from STOP2
/**
 * @brief Set LPTIM4 compare value for wakeup from STOP2
 * @param TicksFromNow Number of ticks until wakeup (at 1024 Hz)
 * @return true if successful
 */
HAL_StatusTypeDef HAL_SetWakeup(uint32_t TicksFromNow)
{
    if (TicksFromNow == 0 || TicksFromNow > 65535) {
        return HAL_ERROR;
    }

    /* Get current counter value */
    uint32_t currentCount = HAL_LPTIM_ReadCounter(&hlptim4);

    /* Calculate compare value (with wraparound) */
    uint32_t compare_value = (currentCount + TicksFromNow) & 0xFFFF;

    /* Ensure CCR1 != ARR (hardware limitation) */
    if (compare_value >= 65535) {
        compare_value = 65534;
    }

    /* Disable CC1 interrupt */
    __HAL_LPTIM_DISABLE_IT(&hlptim4, LPTIM_IT_CC1);

    /* Clear any pending CC1 flag */
    __HAL_LPTIM_CLEAR_FLAG(&hlptim4, LPTIM_FLAG_CC1);

    /* Set compare value in CCR1 */
    /* Note: CCR1 write requires LPTIM to be enabled */
    LPTIM4->CCR1 = compare_value;

    /* Wait for CCR1 update to be acknowledged */
    uint32_t timeout = 10000;
    while (!__HAL_LPTIM_GET_FLAG(&hlptim4, LPTIM_FLAG_CMP1OK) && timeout--) {
        /* Wait for synchronization (~3 LPTIM clock cycles) */
    }

    if (timeout == 0) {
        return HAL_ERROR;
    }

    /* Clear CMP1OK flag */
    __HAL_LPTIM_CLEAR_FLAG(&hlptim4, LPTIM_FLAG_CMP1OK);

    /* Enable CC1 interrupt */
    __HAL_LPTIM_ENABLE_IT(&hlptim4, LPTIM_IT_CC1);


    return HAL_OK;
}

/**
 * @brief LPTIM4 interrupt handler
 */
extern void LPTIM4_IRQHandler(void)
{
    /* Check for Capture/Compare 1 interrupt (wakeup) */
    if (__HAL_LPTIM_GET_FLAG(&hlptim4, LPTIM_FLAG_CC1) &&
        __HAL_LPTIM_GET_IT_SOURCE(&hlptim4, LPTIM_IT_CC1))
    {
        /* Clear the CC1 flag */
        __HAL_LPTIM_CLEAR_FLAG(&hlptim4, LPTIM_FLAG_CC1);

        /* Disable the CC1 interrupt to prevent retriggering */
        __HAL_LPTIM_DISABLE_IT(&hlptim4, LPTIM_IT_CC1);

        /* System wakes from STOP2 automatically */
    }

    /* Process potential interrupts */
    HAL_LPTIM_IRQHandler(&hlptim4);
}
#endif

