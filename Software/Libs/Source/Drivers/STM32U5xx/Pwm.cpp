/**
 ******************************************************************************
 * @file    Pwm.cpp
 * @brief   PWM driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Pwm.hpp"

#define LOG_MODULE_PRX      "Pwm"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include <cassert>

namespace Driver
{

#define LOG_HAL_ERR_MSG(port, msg, err) \
    LOG_ERROR("PORT_%u: %s failed. HAL Err %d\n", static_cast<uint8_t>(port) + 1, msg, err);

Pwm& Pwm::GetInstance(Pwm::Port port)
{
    assert(port < Pwm::Port::PORT_NUM);

    uint8_t n = static_cast<uint8_t>(port);
    static Pwm sInstance[static_cast<uint32_t>(Port::PORT_NUM)];
    sInstance[n].mPort = port;
    return sInstance[n];
}

bool Pwm::init()
{
    switch (mPort) {
        case Port::PORT_1:
            return TIMx_Init(TIM4);

        default:
            assert(false);
            break;
    }

    return false;
}

void Pwm::deinit()
{
    TIMx_DeInit();
}

bool Pwm::start(bool continuously)
{
    (void) continuously;

    HAL_StatusTypeDef err = HAL_ERROR;
    err = HAL_TIM_PWM_Start(&mTimHandle, TIM_CHANNEL_3);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_TIM_PWM_Start", err);
        return false;
    }

    return true;
}

void Pwm::stop()
{
    HAL_StatusTypeDef err = HAL_ERROR;
    err = HAL_TIM_PWM_Stop(&mTimHandle, TIM_CHANNEL_3);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_TIM_PWM_Stop", err);
    }
}

bool Pwm::setDuty(float dutyPct, uint32_t ch)
{
    assert(ch < mChannelsNum);

    uint32_t duty = dutyToTicks(dutyPct);

    __HAL_TIM_SET_COMPARE(&mTimHandle, TIM_CHANNEL_3, duty);
    mDuty[ch] = dutyPct;

    return true;
}

float Pwm::getDuty(uint32_t ch)
{
    assert(ch < mChannelsNum);

    return mDuty[ch];
}

bool Pwm::setFrequency(float frqHz)
{
    // Written as an in-range test so that NaN fails it too.
    if (!(frqHz >= kMinFrequencyHz && frqHz <= kMaxFrequencyHz)) {
        return false;
    }

    mFrequency = frqHz;

    if (mIsInitialized) {
        calcTimebase();
        __HAL_TIM_SET_PRESCALER(&mTimHandle, mTimHandle.Init.Prescaler);
        __HAL_TIM_SET_AUTORELOAD(&mTimHandle, mTimHandle.Init.Period);
        __HAL_TIM_SET_COMPARE(&mTimHandle, TIM_CHANNEL_3, dutyToTicks(mDuty[0]));
        // PSC is always buffered: an update event loads it now rather than
        // at the next overflow. It also restarts the counter, which costs at
        // most one period of the old frequency.
        HAL_TIM_GenerateEvent(&mTimHandle, TIM_EVENTSOURCE_UPDATE);
    }

    return true;
}

void Pwm::calcTimebase()
{
    const uint32_t clk   = HAL_RCC_GetHCLKFreq();
    const uint32_t ticks = static_cast<uint32_t>(clk / mFrequency);
    const uint32_t psc   = (ticks - 1u) / 65536u;

    mTimHandle.Init.Prescaler = psc;
    mTimHandle.Init.Period    = static_cast<uint32_t>(clk / ((psc + 1u) * mFrequency)) - 1u;
}

uint32_t Pwm::dutyToTicks(float dutyPct)
{
    return (uint32_t) ((dutyPct * mTimHandle.Init.Period) / 100.0);
}

bool Pwm::TIMx_Init(TIM_TypeDef *pInstance)
{
    if (mIsInitialized) {
        return true;
    }

    HAL_StatusTypeDef err = HAL_ERROR;

    mTimHandle.Instance               = pInstance;
    mTimHandle.Init.CounterMode       = TIM_COUNTERMODE_UP;
    calcTimebase();
    mTimHandle.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    mTimHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    mTimHandle.PWM_MspInitCallback   = MspInitCallback;
    mTimHandle.PWM_MspDeInitCallback = MspDeInitCallback;

    err = HAL_TIM_PWM_Init(&mTimHandle);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_TIM_PWM_Init", err);
        return false;
    }

    TIM_MasterConfigTypeDef sMasterConfig = { 0 };
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    err = HAL_TIMEx_MasterConfigSynchronization(&mTimHandle, &sMasterConfig);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_TIMEx_MasterConfigSynchronization", err);
        return false;
    }

    TIM_OC_InitTypeDef sConfigOC = { 0 };
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    err = HAL_TIM_PWM_ConfigChannel(&mTimHandle, &sConfigOC, TIM_CHANNEL_3);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_TIM_PWM_ConfigChannel", err);
        return false;
    }

    MspPostInit(&mTimHandle);

    mIsInitialized = true;

    return true;
}


void Pwm::TIMx_DeInit()
{
    if (!mIsInitialized){
        return;
    }

    HAL_TIM_PWM_DeInit(&mTimHandle);
    mIsInitialized = false;
}

void Pwm::MspInitCallback(TIM_HandleTypeDef *timHandle)
{
    if (timHandle->Instance == TIM4) {
        __HAL_RCC_TIM4_CLK_ENABLE();

    }
}

void Pwm::MspPostInit(TIM_HandleTypeDef *timHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };
    if (timHandle->Instance == TIM4) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        // TIM4 GPIO Configuration
        // PB8     ------> TIM4_CH3
        GPIO_InitStruct.Pin = BUZZER_PWM_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF2_TIM4;
        HAL_GPIO_Init(BUZZER_PWM_GPIO_Port, &GPIO_InitStruct);

    }
}

void Pwm::MspDeInitCallback(TIM_HandleTypeDef *timHandle)
{
    if (timHandle->Instance == TIM4) {
        __HAL_RCC_TIM4_CLK_DISABLE();

    }
}

} /* namespace Driver */
