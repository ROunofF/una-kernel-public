/**
 ******************************************************************************
 * @file    IPwm.hpp
 * @brief   PWM timer interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __INTERFACE_I_PWM_HPP
#define __INTERFACE_I_PWM_HPP

#include <cstdint>
#include <cstdbool>

#include "ITim.hpp"

namespace Interface
{

/**
 * @brief   PWM timer interface.
 */
class IPwm : virtual public ITim {

public:

    /**
     * @brief   Get available channels number for the port.
     * @retval  Channels number.
     */
    virtual uint32_t getChannelsNum() = 0;

    /**
     * @brief   Set duty cycle.
     * @param   dutyPct: duty cycle in presents 0 - 100.0
     * @param   ch: target channel.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool setDuty(float dutyPct, uint32_t ch) = 0;

    /**
     * @brief   Get duty cycle.
     * @param   ch: target channel.
     * @retval  duty cycle in presents 0 - 100.0
     */
    virtual float getDuty(uint32_t ch) = 0;

    /**
     * @brief   Start PWM on selected channel.
     * @note    To receive the timer overflow event, and pulse finished event
     *          you need to set the interrupt handler using the
     *          setIrqHandler() method.
     * @param   ch: target channel.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool startChannel(uint32_t ch) = 0;

    /**
     * @brief   Stop PWM on selected channel.
     * @param   ch: target channel.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual void stopChannel(uint32_t ch) = 0;

    /**
     * @brief   PWM interrupt handler interface.
     */
    class IrqHandler {
    public:

        /**
         * @brief   Pulse handler.
         * @param   pPwm: pointer to the IPwm instance.
         * @param   ch: active channel.
         * @param   state: 'true' pulse start, 'false' pulse finished.
         */
        virtual void pulse(Interface::IPwm *pPwm, uint32_t ch, bool state) = 0;
    protected:
        virtual ~IrqHandler() = default;
    };

    /**
     * @brief   Set timer pulse finished interrupt handler.
     * @param   pHandler: interrupt handler IPwm::IrqHandler.
     */
    virtual void setPwmIrqHandler(Interface::IPwm::IrqHandler *pHandler) = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~IPwm() = default;

};

} /* namespace Interface */

#endif /* __INTERFACE_I_PWM_HPP */
