/**
 ******************************************************************************
 * @file    IGpio.hpp
 * @brief   General Purposes Inputs/Outputs (GPIO) interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __INTERFACE_I_GPIO_HPP
#define __INTERFACE_I_GPIO_HPP

#include <cstdint>
#include <cstdbool>

namespace Interface
{

/**
 * @brief   General Purposes Inputs (GPI) interface.
 */
class IGpi
{

public:

    /**
     * @brief   Pull.
     */
    enum class Pull {
        NO_PULL = 0,    ///< No pull
        UP,             ///< Pull UP
        DOWN,           ///< Pull DOWN
    };

    /**
     * @brief   Initialize pin as input.
     * @param   pull: Pull settings Pull.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init(IGpi::Pull pull) = 0;

    /**
     * @brief   Deinitialize pin.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool deinit() = 0;

    /**
     * @brief   Get value of the pin.
     * @retval  'true' pin is set, 'false' otherwise.
     */
    virtual bool get() = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~IGpi() = default;

};

/**
 * @brief   General Purposes Outputs (GPO) interface.
 */
class IGpo
{

public:

    /**
     * @brief   Output type.
     */
    enum class Type {
        PP = 0,     ///< Push-pull
        OD,         ///< Open drain
    };

    /**
     * @brief   Initialize pin as output.
     * @param   type: output type Type.
     * @param   initState: pin state after initialization.
     * @param   odPullup: 'true' enable pull-up on open drain output.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init(IGpo::Type type, bool initState,
            bool odPullup = false) = 0;

    /**
     * @brief   Deinitialize pin.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool deinit() = 0;

    /**
     * @brief   Get value of the pin.
     * @retval  'true' pin is set, 'false' otherwise.
     */
    virtual bool get() = 0;

    /**
     * @brief   Set value of the pin.
     * @param   value: 'true' pin set, 'false' pin reset.
     * @retval  'true' if pin has been set, 'false' otherwise.
     */
    virtual bool set(bool value) = 0;

    /**
     * @brief   Toggle value of the pin.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool toggle() = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~IGpo() = default;

};

/**
 * @brief   Common GPIO interface.
 */
class IGpio : virtual public IGpi, virtual public IGpo
{

public:

    /**
     * @brief   Deinitialize pin.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool deinit() = 0;

    /**
     * @brief   Get value of the pin.
     * @retval  'true' pin is set, 'false' otherwise.
     */
    virtual bool get() = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~IGpio() = default;

};

/**
 * @brief   External Interrupt Inputs (EXTI) interface.
 */
class IExti : virtual public IGpio
{

public:

    /**
     * @brief   Interrupt edge.
     */
    enum class Edge {
        RISING = 0,         ///< Rising only
        FALLING,            ///< Falling only
        RISING_FALLING,     ///< Rising and falling
    };

    /**
     * @brief   Initialize pin as input.
     * @param   pull: Pull settings Pull.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init(IGpi::Pull pull) = 0;

    /**
     * @brief   Initialize pin as output.
     * @param   type: output type Type.
     * @param   initState: pin state after initialization.
     * @param   odPullup: 'true' enable pull-up on open drain output.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init(IGpo::Type type, bool initState,
            bool odPullup = false) = 0;

    /**
     * @brief   Initialize pin as external interrupt.
     * @param   pull: Pull settings Pull.
     * @param   edge: Interrupt edge Edge.
     * @param   priority: interrupt priority.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init(IGpi::Pull pull, IExti::Edge edge, uint32_t priority) = 0;

    /**
     * @brief   Enable interrupt.
     * @param   isEnable: 'true' enable interrupt, 'false' disable.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool enableInt(bool isEnable) = 0;

    /**
     * @brief   Clear pending interrupt.
     */
    virtual void clearInt() = 0;

    /**
     * @brief   External Interrupt Inputs (EXTI) handler interface.
     */
    class Handler
    {

    public:

        /**
         * @brief   External interrupt handler.
         * @param   pExti: pointer to the external interrupt instance IExti.
         * @param   edge: Interrupt edge Edge.
         */
        virtual void extiHandler(Interface::IExti *pExti, Interface::IExti::Edge edge) = 0;

    protected:
        virtual ~Handler() = default;

    };

    /**
     * @brief   Set external interrupt handler.
     * @param   pHandler: external interrupt handler IExtiHandler.
     */
    virtual void setExtiHandler(Interface::IExti::Handler *pHandler) = 0;


protected:

    /**
     * @brief   Destructor.
     */
    virtual ~IExti() = default;

};



} /* namespace Interface */

#endif /* __INTERFACE_I_GPIO_HPP */
