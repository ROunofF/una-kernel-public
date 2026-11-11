/**
 ******************************************************************************
 * @file    ITim.hpp
 * @brief   Timer (TIM) interface
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __INTERFACE_I_TIM_HPP
#define __INTERFACE_I_TIM_HPP

#include <cstdint>
#include <cstdbool>

namespace Interface
{

/**
 * @brief   Timer (TIM) interface
 */
class ITim {

public:

    /**
     * @brief   Initialization of the TIM instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init() = 0;

    /**
     * @brief   Deinitialization of the TIM instance.
     */
    virtual void deinit() = 0;

    /**
     * @brief   Set target frequency.
     * @param   frqHz: frequency in Hz.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool setFrequency(float frqHz) = 0;

    /**
     * @brief   Get current frequency.
     * @param   frqHz: frequency in Hz.
     * @retval  Currently set frequency in Hz.
     */
    virtual float getFrequency() = 0;

    /**
     * @brief   Start timer in interrupt mode.
     * @note    To receive the timer overflow event, you need to set the
     *          interrupt handler using the setIrqHandler() method.
     * @param   continuously: 'true' - the timer will generate events until
     *          the stop() method is called, 'false' - the timer will generate
     *          only one event.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool start(bool continuously = false) = 0;

    /**
     * @brief   Stop timer in interrupt mode.
     */
    virtual void stop() = 0;

    /**
     * @brief   TIM interrupt handler interface.
     */
    class IrqHandler {
    public:
        /**
         * @brief   External interrupt handler.
         * @param   pTim: pointer to the ITim instance.
         */
        virtual void periodElapsed(Interface::ITim *pTim) = 0;
    protected:
        virtual ~IrqHandler() = default;
    };

    /**
     * @brief   Set timer overflow interrupt handler.
     * @param   pHandler: interrupt handler ITimIrqHandler.
     */
    virtual void setIrqHandler(Interface::ITim::IrqHandler *pHandler) = 0;

    /**
     * @brief   Destructor.
     */
    virtual ~ITim() = default;

};

} /* namespace Interface */

#endif /* __INTERFACE_I_TIM_HPP */
