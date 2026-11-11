/**
 ******************************************************************************
 * @file    Pwm.hpp
 * @brief   PWM driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __DRIVER_PWM_HPP
#define __DRIVER_PWM_HPP

#include <cstdint>
#include <cstdbool>

#include "IPwm.hpp"
#include "main.h"

namespace Driver
{

/**
 * @brief   PWM driver.
 */
class Pwm : virtual public Interface::IPwm
{

public:

    /**
     * @brief   Available ports.
     */
    enum class Port : uint8_t {
        PORT_1 = 0,

        PORT_NUM    // Not a valid parameter
    };

    /**
     * @brief   Get driver instance.
     * @param   port: target port.
     * @retval  Pointer to the driver instance.
     */
    static Pwm& GetInstance(Port port);

    /**
     * @brief   Initialization of the TIM instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init() override;

    /**
     * @brief   Deinitialization of the TIM instance.
     */
    virtual void deinit() override;

    /**
     * @brief   Set target frequency.
     * @note    Takes effect immediately, including while the timer runs. The
     *          duty cycle of each channel is kept.
     * @param   frqHz: frequency in Hz, kMinFrequencyHz to kMaxFrequencyHz.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool setFrequency(float frqHz) override;

    /**
     * @brief   Get current frequency.
     * @param   frqHz: frequency in Hz.
     * @retval  Currently set frequency in Hz.
     */
    virtual float getFrequency() override
    {
        return mFrequency;
    }

    /**
     * @brief   Start timer in interrupt mode.
     * @note    To receive the timer overflow event, you need to set the
     *          interrupt handler using the setIrqHandler() method.
     * @param   continuously: not used in this driver
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool start(bool continuously = false) override;

    /**
     * @brief   Stop timer in interrupt mode.
     */
    virtual void stop() override;

    /**
     * @brief   Set timer overflow interrupt handler.
     * @param   pHandler: interrupt handler ITimIrqHandler.
     */
    virtual void setIrqHandler(Interface::ITim::IrqHandler *pHandler) override
    {
        // Not implemented
    }

    /**
     * @brief   Get available channels number for the port.
     * @retval  Channels number.
     */
    virtual uint32_t getChannelsNum() override
    {
        return mChannelsNum;
    }

    /**
     * @brief   Set duty cycle.
     * @param   dutyPct: duty cycle in presents 0 - 100.0
     * @param   ch: target channel.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool setDuty(float dutyPct, uint32_t ch) override;

    /**
     * @brief   Get duty cycle.
     * @param   ch: target channel.
     * @retval  duty cycle in presents 0 - 100.0
     */
    virtual float getDuty(uint32_t ch) override;

    /**
     * @brief   Start PWM on selected channel.
     * @note    To receive the timer overflow event, and pulse finished event
     *          you need to set the interrupt handler using the
     *          setIrqHandler() method.
     * @param   ch: target channel.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool startChannel(uint32_t ch) override
    {
        // Not implemented. Use start().
        return start();
    }

    /**
     * @brief   Stop PWM on selected channel.
     * @param   ch: target channel.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual void stopChannel(uint32_t ch) override
    {
        // Not implemented. Use stop().
        stop();
    }

    /**
     * @brief   Set timer pulse finished interrupt handler.
     * @param   pHandler: interrupt handler IPwmIrqHandler.
     */
    virtual void setPwmIrqHandler(Interface::IPwm::IrqHandler *pHandler) override
    {
        // Not implemented.
    }

    /**
     * @brief   Interrupt handler for this driver.
     * @note    Call this method from stm32h7xx_it.c/cpp file.
     */
    static void IrqHandler()
    {
        // Not implemented.
    }

private:

    // Port number
    Port mPort { };

    // HAL TIM object
    TIM_HandleTypeDef mTimHandle { };

    // Indicates that the TIM port is initialized
    bool mIsInitialized { };

    // Maximum PWM channels supported by this driver
    static const uint32_t kMaxChannelsNum = 1;

    // Actual number of the channels for the current port
    uint32_t mChannelsNum { kMaxChannelsNum };

    // Duty cycle for each channel
    float mDuty[kMaxChannelsNum] { };

    float mFrequency = 2500;

    static constexpr float kMinFrequencyHz = 500.0f;
    static constexpr float kMaxFrequencyHz = 20000.0f;

    /**
     * @brief   Constructor.
     */
    Pwm() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~Pwm()
    {
        deinit();
    }

    /**
     * @brief   Convert duty cycle to timer ticks.
     * @param   dutyPct: duty cycle in presents 0 - 100.0
     * @retval  Timer ticks.
     */
    uint32_t dutyToTicks(float dutyPct);

    /**
     * @brief   Work out the prescaler and period for mFrequency and store
     *          them in mTimHandle.Init. The prescaler is the smallest that
     *          keeps the period within 16 bits, so the driver also works on
     *          the U5's 16-bit timers; TIM4 itself is 32-bit.
     */
    void calcTimebase();

    /**
     * @brief   Initialization of the TIMx instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool TIMx_Init(TIM_TypeDef *pInstance);

    /**
     * @brief   Deinitialization of the TIMx instance.
     */
    void TIMx_DeInit();

    // HAL Callback.
    static void MspInitCallback(TIM_HandleTypeDef *timHandle);
    static void MspPostInit(TIM_HandleTypeDef* timHandle);
    static void MspDeInitCallback(TIM_HandleTypeDef *timHandle);

};

} /* namespace Driver */

#endif /* __DRIVER_PWM_HPP */
