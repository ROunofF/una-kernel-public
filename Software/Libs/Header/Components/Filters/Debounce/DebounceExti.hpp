/**
 ******************************************************************************
 * @file    DebounceExti.hpp
 * @brief   Debounce filter class that can linked with EXTI.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __FILTER_DEBOUNCE_EXTI_HPP
#define __FILTER_DEBOUNCE_EXTI_HPP

#include "Debounce.hpp"
#include "IGpio.hpp"

namespace Filter
{

/**
 * @brief   %Debounce filter class that can linked with EXTI.
 */
class DebounceExti : virtual public Debounce, virtual public Interface::IExti,
        virtual public Interface::IExti::Handler
{

public:

    /**
     * @brief   Constructor
     */
    DebounceExti() = default;

    /**
     * @brief   Constructor
     * @param   pExti: pointer to the Interface::IExti object.
     */
    DebounceExti(Interface::IExti *pExti)
    {
        linkExtiObj(pExti);
    }

    /**
     * @brief   Destructor
     */
    virtual ~DebounceExti() = default;

    /**
     * @brief   Link DebounceExti with Interface::IExti object.
     * @param   pExti: pointer to the Interface::IExti object.
     */
    void linkExtiObj(Interface::IExti *pExti)
    {
        mpExti = pExti;
        mpExti->setExtiHandler(this);
    }

    /**
     * @brief   Pass initialization to the IExti object.
     * @param   pull: Pull settings Interface::IGpi::Pull.
     * @param   edge: Interrupt edge Interface::IExti::Edge.
     * @param   priority: interrupt priority.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init(IGpi::Pull pull, IExti::Edge edge, uint32_t priority)
            override
    {
        if (mpExti != nullptr) {
            return mpExti->init(pull, edge, priority);
        }
        return false;
    }

    /**
     * @brief   Enable interrupt.
     * @param   isEnable: 'true' enable interrupt, 'false' disable.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool enableInt(bool isEnable) override
    {
        if (mpExti != nullptr) {
            return mpExti->enableInt(isEnable);
        }
        return false;
    }

    /**
     * @brief   Clear pending interrupt.
     */
    virtual void clearInt() override
    {
        if (mpExti != nullptr) {
            return mpExti->clearInt();
        }
    }

    /**
     * @brief   Set external interrupt handler.
     * @param   pHandler: external interrupt handler Interface::IExtiHandler.
     */
    virtual void setExtiHandler(Interface::IExti::Handler *pHandler) override
    {
        mpExtiHandler = pHandler;
    }

    /**
     * @brief   Pass pin deinitialization to the Interface::IExti object.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool deinit() override
    {
        if (mpExti != nullptr) {
            return mpExti->deinit();
        }
        return false;
    }

    /**
     * @brief   Get filtered value of the pin.
     * @retval  'true' pin is set, 'false' otherwise.
     */
    virtual bool get() override
    {
        return Debounce::get();
    }

protected:
    /**
     * @brief   Method that called when filter out is changed.
     * @param   output state 'true' pin is set, 'false' otherwise.
     */
    virtual void onOutChange(bool out)
    {
        if (mpExti != nullptr && mpExtiHandler != nullptr) {
            mpExtiHandler->extiHandler(mpExti, out ? IExti::Edge::RISING : IExti::Edge::FALLING);
        }

        Debounce::onOutChange(out);
    }

private:

    // Pointer to the Exti object
    Interface::IExti *mpExti = nullptr;

    // External interrupt handler
    Interface::IExti::Handler *mpExtiHandler = nullptr;

    /**
     * @param   Hidden initialization of a normal input.
     * @param   pull: Pull settings Interface::IGpi::Pull.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init(IGpi::Pull pull)
    {
        return false;
    }

    /**
     * @brief   Initialize pin as output.
     * @param   type: output type Type.
     * @param   initState: pin state after initialization.
     * @param   odPullup: 'true' enable pull-up on open drain output.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init(IGpo::Type type, bool initState,
            bool odPullup = false) override
    {
        return false;
    }

    /**
     * @brief   Set value of the pin.
     * @param   value: 'true' pin set, 'false' pin reset.
     * @retval  'true' if pin has been set, 'false' otherwise.
     */
    virtual bool set(bool value) override
    {
        return false;
    }

    /**
     * @brief   Toggle value of the pin.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool toggle() override
    {
        return false;
    }

    /**
     * @brief   External interrupt handler.
     * @param   pExti: pointer to the external interrupt instance Interface::IExti.
     * @param   edge: Interrupt edge Edge.
     */
    virtual void extiHandler(Interface::IExti *pExti,
            Interface::IExti::Edge edge) override
    {
        add(mpExti->get());
    }

};

} /* namespace Filter */

#endif /* __FILTER_DEBOUNCE_EXTI_HPP */
