/**
 ******************************************************************************
 * @file    Gpio.hpp
 * @brief   GPIO driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __DRIVER_GPIO_HPP
#define __DRIVER_GPIO_HPP

#include <cstdint>
#include <cstdbool>

#include "IGpio.hpp"

#include "main.h"

namespace Driver
{

/**
 * @brief   GPIO Driver
 */
class Gpio : public Interface::IExti
{

public:

    /**
     * @brief   Available pins.
     */
    enum class Pin {

        // MAIN I2C bus
        MAIN_I2C_SCL,
        MAIN_I2C_SDA,

        // BUTTONS
        SW1,                // EXTI
        PWR_ON_1V8_L,       // EXTI
        SW3,                // EXTI
        SW4,                // EXTI

        // EMMC
        EMMC_EN,
        SDIO_CK,
        SDIO_CMD,
        SDIO_D0,
        SDIO_D1,
        SDIO_D2,
        SDIO_D3,

        // VIBRO
        HAPTIC_RST_N,

        // BUZZER
        BUZZER_EN1,
        BUZZER_EN2,
        BUZZER_PWM,

        // FUEL GAUGE
        FUEL_GAUGE_ALRT,

        // PMIC
        PWR_INT_L,          // EXTI

        // USB Protection
        USB_FAULT_L,        // EXTI
        USB_DAM,

        // USB
        USB_D_P,
        USB_D_N,

        // Debug TX
        DEBUG_TX,


        // LCD
        LCD_FRP,
        LCD_XFRP,
        LCD_QSPI_CS_VCK,
        LCD_QSPI_SCK_VST,
        LCD_QSPI_IO0_XRST,
        LCD_QSPI_IO1_HST,
        LCD_QSPI_IO2_ENB,
        LCD_QSPI_IO3_HCK,
        LCD_R1,
        LCD_R2,
        LCD_B1,
        LCD_B2,
        LCD_G1,
        LCD_G2,

        // TOUCHSCREEN
        TOUCH_INT_L,        // EXTI
        TOUCH_RST_L,

        // DISPLAY BACKLIGHT
        BACKLIGHT_ON,

        // SENSOR BODY
        SENSORBODY_INT0,    // EXTI
        SENSORBODY_INT1,    // EXTI
        SENSORBODY_INT2,    // EXTI
        SENSORBODY_INT3,    // EXTI
        SENSORBODY_RST_L,
        SENSORBODY_CS,
        SENSORBODY_SCK,
        SENSORBODY_MOSI,
        SENSORBODY_MISO,
        SENSORBODY_SCL,
        SENSORBODY_SDA,

        // ALTIMETER
        ALTIMETER_EN,
        //ALTIMETER_INT, // USB_FAULT_L from board UNAcore 3.0

        // SENSOR 1
        SENSOR1_TX_INT0,    // EXTI
        SENSOR1_RX_INT1,    // EXTI
        SENSOR1_INT2,       // EXTI
        SENSOR1_RST_L,
        SENSOR1_CS,
        SENSOR1_SCK,
        SENSOR1_MOSI,
        SENSOR1_MISO,
        SENSOR1_SCL,
        SENSOR1_SDA,

        // SENSOR 2
        SENSOR2_TX_INT0,    // EXTI
        SENSOR2_RX_INT1,    // EXTI
        SENSOR2_INT2,       // EXTI
        SENSOR2_RST_L,
        SENSOR2_CS,
        SENSOR2_SCK,
        SENSOR2_MOSI,
        SENSOR2_MISO,
        SENSOR2_SCL,
        SENSOR2_SDA,

        PINS_NUM   // Not a valid parameter
    };

    /**
     * @brief   Get driver instance.
     * @param   port: target port.
     * @retval  GPIO instance.
     */
    static Gpio& GetInstance(Pin pin);

    /**
     * @brief   Initialize pin as input.
     * @param   pull: Pull settings @Pull.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init(IGpi::Pull pull) override;

    /**
     * @brief   Initialize pin as external interrupt.
     * @param   pull: Pull settings @Pull.
     * @param   edge: Interrupt edge @Edge.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init(IGpi::Pull pull, IExti::Edge edge, uint32_t priority) override;

    /**
     * @brief   Initialize pin as output.
     * @param   type: output type @Type.
     * @param   initState: pin state after initialization.
     * @param   odPullup: 'true' enable pull-up on open drain output.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init(IGpo::Type type, bool initState, bool odPullup = false) override;

    /**
     * @brief   Deinitialize pin.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool deinit() override;

    /**
     * @brief   Get value of the pin.
     * @retval  'true' pin is set, 'false' otherwise.
     */
    virtual bool get() override;

    /**
     * @brief   Set value of the pin.
     * @param   value: 'true' pin set, 'false' pin reset.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool set(bool value) override;

    /**
     * @brief   Toggle value of the pin.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool toggle() override;

    /**
     * @brief   Enable interrupt.
     * @param   isEnable: 'true' enable interrupt, 'false' disable.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool enableInt(bool isEnable) override;

    /**
     * @brief   Clear pending interrupt.
     */
    virtual void clearInt() override;

    /**
     * @brief   Set external interrupt handler.
     * @param   pHandler: external interrupt handler IExtiHandler.
     */
    virtual void setExtiHandler(Interface::IExti::Handler *pHandler) override
    {
        mpExtiHandler = pHandler;
    }

    /**
     * @brief   Interrupt handler for a single EXTI line.
     * @param   pin: pin bit-mask of the line (GPIO_PIN_0..GPIO_PIN_15); each
     *               line has its own NVIC vector.
     */
    static void IrqHandler(uint32_t pin);


private:

    // Number of interrupt lines
    static const uint32_t skIrqsNum = 16;

    // Mask of enabled interrupts
    static uint32_t sIrqMask;

    // Interrupt handlers for each line
    static Gpio *sIrqLineHandlers[skIrqsNum];

    /**
     * @brief   HAL Pin structure.
     */
    typedef struct {
        GPIO_TypeDef *port;
        uint32_t pin;
    } GpioPin_t;

    // Pins definition
    static const GpioPin_t skPinsDef[];

    // HAL GPIO Pin
    GpioPin_t mPin { };

    // External interrupt handler
    Interface::IExti::Handler *mpExtiHandler { };


    /**
     * @brief   Constructor.
     */
    Gpio() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~Gpio() = default;

    /**
     * @brief   Assign pin to the object.
     */
    void assignPin(GpioPin_t pin);

    /**
     * @brief   Get pin number 0-15.
     * @param   pin: gpio pin (bit mask)
     * @retval  Pin number.
     */
    static uint32_t getPinNum(uint32_t pin);

    /**
     * @brief   Get interrupt number.
     * @retval  Interrupt Number.
     */
    IRQn_Type getIrqn();

    /**
     * @brief   Enable/Disable clock for GPIO.
     * @param   port: GPIO port.
     * @param   isEnable: 'true' enable clock, 'false' disable clock.
     */
    void clk(GPIO_TypeDef *port, bool isEnable);

};

} /* namespace Drivers */

#endif /* __DRIVER_GPIO_HPP */
