/**
 ******************************************************************************
 * @file    Gpio.сpp
 * @brief   GPIO driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Gpio.hpp"

#define LOG_MODULE_PRX      "Gpio"
#define LOG_MODULE_LEVEL    LOG_LEVEL_ERROR
#include "Logger.h"

#include <cassert>

namespace Driver
{

// Static fields
const Gpio::GpioPin_t Gpio::skPinsDef[static_cast<uint32_t>(Pin::PINS_NUM)] =
{
 [static_cast<uint32_t>(Pin::MAIN_I2C_SCL)] = { MAIN_SCL_GPIO_Port, MAIN_SCL_Pin },
 [static_cast<uint32_t>(Pin::MAIN_I2C_SDA)] = { MAIN_SDA_GPIO_Port, MAIN_SDA_Pin },

 [static_cast<uint32_t>(Pin::SW1)] = { SW1_GPIO_Port, SW1_Pin },
 [static_cast<uint32_t>(Pin::PWR_ON_1V8_L)] = { PWR_ON_1V8_L_GPIO_Port, PWR_ON_1V8_L_Pin },
 [static_cast<uint32_t>(Pin::SW3)] = { SW3_GPIO_Port, SW3_Pin },
 [static_cast<uint32_t>(Pin::SW4)] = { SW4_GPIO_Port, SW4_Pin },

 [static_cast<uint32_t>(Pin::EMMC_EN)] = { EMMC_EN_GPIO_Port, EMMC_EN_Pin },

 [static_cast<uint32_t>(Pin::SDIO_CK)] = { SDIO_CK_GPIO_Port, SDIO_CK_Pin },
 [static_cast<uint32_t>(Pin::SDIO_CMD)] = { SDIO_CMD_GPIO_Port, SDIO_CMD_Pin },
 [static_cast<uint32_t>(Pin::SDIO_D0)] = { SDIO_D0_GPIO_Port, SDIO_D0_Pin },
 [static_cast<uint32_t>(Pin::SDIO_D1)] = { SDIO_D1_GPIO_Port, SDIO_D1_Pin },
 [static_cast<uint32_t>(Pin::SDIO_D2)] = { SDIO_D2_GPIO_Port, SDIO_D2_Pin },
 [static_cast<uint32_t>(Pin::SDIO_D3)] = { SDIO_D3_GPIO_Port, SDIO_D3_Pin },

 [static_cast<uint32_t>(Pin::HAPTIC_RST_N)] = { HAPTIC_RST_N_GPIO_Port, HAPTIC_RST_N_Pin },

 [static_cast<uint32_t>(Pin::BUZZER_EN1)] = { BUZZER_EN1_GPIO_Port, BUZZER_EN1_Pin },
 [static_cast<uint32_t>(Pin::BUZZER_EN2)] = { BUZZER_EN2_GPIO_Port, BUZZER_EN2_Pin },
 [static_cast<uint32_t>(Pin::BUZZER_PWM)] = { BUZZER_PWM_GPIO_Port, BUZZER_PWM_Pin },

 [static_cast<uint32_t>(Pin::FUEL_GAUGE_ALRT)] = { FUEL_GAUGE_ALRT_GPIO_Port, FUEL_GAUGE_ALRT_Pin },

 [static_cast<uint32_t>(Pin::PWR_INT_L)] = { PWR_INT_L_GPIO_Port, PWR_INT_L_Pin },

 [static_cast<uint32_t>(Pin::USB_FAULT_L)] = { USB_FAULT_L_GPIO_Port, USB_FAULT_L_Pin },
 [static_cast<uint32_t>(Pin::USB_DAM)] = { USB_DAM_GPIO_Port, USB_DAM_Pin },

 [static_cast<uint32_t>(Pin::USB_D_P)] = { USB_D_P_GPIO_Port, USB_D_P_Pin },
 [static_cast<uint32_t>(Pin::USB_D_N)] = { USB_D_N_GPIO_Port, USB_D_N_Pin },

 [static_cast<uint32_t>(Pin::DEBUG_TX)] = { DEBUG_TX_GPIO_Port, DEBUG_TX_Pin },

 [static_cast<uint32_t>(Pin::LCD_FRP)] = { LCD_FRP_GPIO_Port, LCD_FRP_Pin },
 [static_cast<uint32_t>(Pin::LCD_XFRP)] = { LCD_XFRP_GPIO_Port, LCD_XFRP_Pin },
 [static_cast<uint32_t>(Pin::LCD_QSPI_CS_VCK)] = { LCD_G2_GPIO_Port, LCD_G2_Pin },
 [static_cast<uint32_t>(Pin::LCD_QSPI_SCK_VST)] = { LCD_QSPI_SCK_VST_GPIO_Port, LCD_QSPI_SCK_VST_Pin },
 [static_cast<uint32_t>(Pin::LCD_QSPI_IO0_XRST)] = { LCD_QSPI_IO0_XRST_GPIO_Port, LCD_QSPI_IO0_XRST_Pin },
 [static_cast<uint32_t>(Pin::LCD_QSPI_IO1_HST)] = { LCD_QSPI_IO1_HST_GPIO_Port, LCD_QSPI_IO1_HST_Pin },
 [static_cast<uint32_t>(Pin::LCD_QSPI_IO2_ENB)] = { LCD_QSPI_IO2_ENB_GPIO_Port, LCD_QSPI_IO2_ENB_Pin },
 [static_cast<uint32_t>(Pin::LCD_QSPI_IO3_HCK)] = { LCD_QSPI_IO3_HCK_GPIO_Port, LCD_QSPI_IO3_HCK_Pin },
 [static_cast<uint32_t>(Pin::LCD_R1)] = { LCD_R1_GPIO_Port, LCD_R1_Pin },
 [static_cast<uint32_t>(Pin::LCD_R2)] = { LCD_R2_GPIO_Port, LCD_R2_Pin },
 [static_cast<uint32_t>(Pin::LCD_B1)] = { LCD_B1_GPIO_Port, LCD_B1_Pin },
 [static_cast<uint32_t>(Pin::LCD_B2)] = { LCD_B2_GPIO_Port, LCD_B2_Pin },
 [static_cast<uint32_t>(Pin::LCD_G1)] = { LCD_G1_GPIO_Port, LCD_G1_Pin },
 [static_cast<uint32_t>(Pin::LCD_G2)] = { LCD_G2_GPIO_Port, LCD_G2_Pin },

 [static_cast<uint32_t>(Pin::TOUCH_INT_L)] = { TOUCH_INT_L_GPIO_Port, TOUCH_INT_L_Pin },
 [static_cast<uint32_t>(Pin::TOUCH_RST_L)] = { TOUCH_RST_L_GPIO_Port, TOUCH_RST_L_Pin },

 [static_cast<uint32_t>(Pin::BACKLIGHT_ON)] = { BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin },

 [static_cast<uint32_t>(Pin::SENSORBODY_INT0)] = { SENSORBODY_INT0_GPIO_Port, SENSORBODY_INT0_Pin },
 [static_cast<uint32_t>(Pin::SENSORBODY_INT1)] = { SENSORBODY_INT1_GPIO_Port, SENSORBODY_INT1_Pin },
 [static_cast<uint32_t>(Pin::SENSORBODY_INT2)] = { SENSORBODY_INT2_GPIO_Port, SENSORBODY_INT2_Pin },
 [static_cast<uint32_t>(Pin::SENSORBODY_INT3)] = { SENSORBODY_INT3_GPIO_Port, SENSORBODY_INT3_Pin },
 [static_cast<uint32_t>(Pin::SENSORBODY_RST_L)] = { SENSORBODY_RST_L_GPIO_Port, SENSORBODY_RST_L_Pin },
 [static_cast<uint32_t>(Pin::SENSORBODY_CS)] = { SENSORBODY_CS_GPIO_Port, SENSORBODY_CS_Pin },
 [static_cast<uint32_t>(Pin::SENSORBODY_SCK)] = { SENSORBODY_SCK_GPIO_Port, SENSORBODY_SCK_Pin },
 [static_cast<uint32_t>(Pin::SENSORBODY_MOSI)] = { SENSORBODY_MOSI_GPIO_Port, SENSORBODY_MOSI_Pin },
 [static_cast<uint32_t>(Pin::SENSORBODY_MISO)] = { SENSORBODY_MISO_GPIO_Port, SENSORBODY_MISO_Pin },
 [static_cast<uint32_t>(Pin::SENSORBODY_SCL)] = { SENSORBODY_SCL_GPIO_Port, SENSORBODY_SCL_Pin },
 [static_cast<uint32_t>(Pin::SENSORBODY_SDA)] = { SENSORBODY_SDA_GPIO_Port, SENSORBODY_SDA_Pin },

 [static_cast<uint32_t>(Pin::ALTIMETER_EN)] = { ALTIMETER_EN_GPIO_Port, ALTIMETER_EN_Pin },
 //[static_cast<uint32_t>(Pin::ALTIMETER_INT)] = { ALTIMETER_INT_GPIO_Port, ALTIMETER_INT_Pin }, // USB_FAULT_L from board UNAcore 3.0

 [static_cast<uint32_t>(Pin::SENSOR1_TX_INT0)] = { SENSOR1_TX_INT0_GPIO_Port, SENSOR1_TX_INT0_Pin },
 [static_cast<uint32_t>(Pin::SENSOR1_RX_INT1)] = { SENSOR1_RX_INT1_GPIO_Port, SENSOR1_RX_INT1_Pin },
 [static_cast<uint32_t>(Pin::SENSOR1_INT2)] = { SENSOR1_INT2_GPIO_Port, SENSOR1_INT2_Pin },
 [static_cast<uint32_t>(Pin::SENSOR1_RST_L)] = { SENSOR1_RST_L_GPIO_Port, SENSOR1_RST_L_Pin },
 [static_cast<uint32_t>(Pin::SENSOR1_CS)] = { SENSOR1_CS_GPIO_Port, SENSOR1_CS_Pin },
 [static_cast<uint32_t>(Pin::SENSOR1_SCK)] = { SENSOR1_SCK_GPIO_Port, SENSOR1_SCK_Pin },
 [static_cast<uint32_t>(Pin::SENSOR1_MOSI)] = { SENSOR1_MOSI_GPIO_Port, SENSOR1_MOSI_Pin },
 [static_cast<uint32_t>(Pin::SENSOR1_MISO)] = { SENSOR1_MISO_GPIO_Port, SENSOR1_MISO_Pin },
 [static_cast<uint32_t>(Pin::SENSOR1_SCL)] = { SENSOR1_SCL_GPIO_Port, SENSOR1_SCL_Pin },
 [static_cast<uint32_t>(Pin::SENSOR1_SDA)] = { SENSOR1_SDA_GPIO_Port, SENSOR1_SDA_Pin },

 [static_cast<uint32_t>(Pin::SENSOR2_TX_INT0)] = { SENSOR2_TX_INT0_GPIO_Port, SENSOR2_TX_INT0_Pin },
 [static_cast<uint32_t>(Pin::SENSOR2_RX_INT1)] = { SENSOR2_RX_INT1_GPIO_Port, SENSOR2_RX_INT1_Pin },
 [static_cast<uint32_t>(Pin::SENSOR2_INT2)] = { SENSOR2_INT2_GPIO_Port, SENSOR2_INT2_Pin },
 [static_cast<uint32_t>(Pin::SENSOR2_RST_L)] = { SENSOR2_RST_L_GPIO_Port, SENSOR2_RST_L_Pin },
 [static_cast<uint32_t>(Pin::SENSOR2_CS)] = { SENSOR2_CS_GPIO_Port, SENSOR2_CS_Pin },
 [static_cast<uint32_t>(Pin::SENSOR2_SCK)] = { SENSOR2_SCK_GPIO_Port, SENSOR2_SCK_Pin },
 [static_cast<uint32_t>(Pin::SENSOR2_MOSI)] = { SENSOR2_MOSI_GPIO_Port, SENSOR2_MOSI_Pin },
 [static_cast<uint32_t>(Pin::SENSOR2_MISO)] = { SENSOR2_MISO_GPIO_Port, SENSOR2_MISO_Pin },
 [static_cast<uint32_t>(Pin::SENSOR2_SCL)] = { SENSOR2_SCL_GPIO_Port, SENSOR2_SCL_Pin },
 [static_cast<uint32_t>(Pin::SENSOR2_SDA)] = { SENSOR2_SDA_GPIO_Port, SENSOR2_SDA_Pin },

};

uint32_t Gpio::sIrqMask;
Gpio *Gpio::sIrqLineHandlers[skIrqsNum];


Gpio& Gpio::GetInstance(Gpio::Pin pin)
{
    assert(pin < Pin::PINS_NUM);

    uint32_t n = static_cast<uint32_t>(pin);
    static Gpio sInstance[static_cast<uint32_t>(Pin::PINS_NUM)];
    sInstance[n].assignPin(sInstance[n].skPinsDef[n]);
    return sInstance[n];
}

bool Gpio::init(IGpi::Pull pull)
{
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };

    clk(mPin.port, true);

    GPIO_InitStruct.Pin = mPin.pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = pull == Pull::UP ? GPIO_PULLUP :
                    (pull == Pull::DOWN ? GPIO_PULLDOWN : GPIO_NOPULL);
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(mPin.port, &GPIO_InitStruct);

    return true;
}

bool Gpio::init(IGpi::Pull pull, IExti::Edge edge, uint32_t priority)
{
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };

    clk(mPin.port, true);

    GPIO_InitStruct.Pin = mPin.pin;
    GPIO_InitStruct.Mode = edge == Edge::RISING ? GPIO_MODE_IT_RISING :
                    (edge == Edge::FALLING ? GPIO_MODE_IT_FALLING :
                    GPIO_MODE_IT_RISING_FALLING);
    GPIO_InitStruct.Pull = pull == Pull::UP ? GPIO_PULLUP :
                    (pull == Pull::DOWN ? GPIO_PULLDOWN : GPIO_NOPULL);
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(mPin.port, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(getIrqn(), priority, 0);
    enableInt(false);
    sIrqLineHandlers[getPinNum(mPin.pin)] = this;

    return true;
}

bool Gpio::init(IGpo::Type type, bool initState, bool odPullup)
{
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };

    clk(mPin.port, true);
    set(initState);

    GPIO_InitStruct.Pin = mPin.pin;
    GPIO_InitStruct.Mode = (type == Type::OD) ? GPIO_MODE_OUTPUT_OD :
                    GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = odPullup ? GPIO_PULLUP : GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(mPin.port, &GPIO_InitStruct);

    return true;
}

bool Gpio::deinit()
{
    if (sIrqLineHandlers[getPinNum(mPin.pin)] == this) {
        enableInt(false);
        mpExtiHandler = nullptr;
    }
    HAL_GPIO_DeInit(mPin.port, mPin.pin);

    return true;
}

bool Gpio::get()
{
    return HAL_GPIO_ReadPin(mPin.port, mPin.pin) == GPIO_PIN_SET;
}

bool Gpio::set(bool value)
{
    HAL_GPIO_WritePin(mPin.port, mPin.pin,
            value ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return true;
}

bool Gpio::toggle()
{
    HAL_GPIO_TogglePin(mPin.port, mPin.pin);
    return true;
}

bool Gpio::enableInt(bool isEnable)
{
    if (isEnable) {
        HAL_NVIC_EnableIRQ(getIrqn());
    } else {
        HAL_NVIC_DisableIRQ(getIrqn());
    }

    return true;
}

void Gpio::clearInt()
{
    __HAL_GPIO_EXTI_CLEAR_IT(mPin.pin);
}

void Gpio::IrqHandler(uint32_t pin)
{
    bool rising  = __HAL_GPIO_EXTI_GET_RISING_IT(pin) != 0u;
    bool falling = __HAL_GPIO_EXTI_GET_FALLING_IT(pin) != 0u;

    // Clear only this line's pending flags, and clear before dispatch (HAL
    // clear-then-handle convention). Touching foreign lines here would erase a
    // concurrent edge caught mid-sweep; clearing after dispatch would erase an
    // edge that re-arrives while the handler reads a latched source. Both cost a
    // permanently lost edge on a latched pin (e.g. BMI270 INT2) - a hard wedge.
    if (rising) {
        __HAL_GPIO_EXTI_CLEAR_RISING_IT(pin);
    }
    if (falling) {
        __HAL_GPIO_EXTI_CLEAR_FALLING_IT(pin);
    }

    Gpio *instance = sIrqLineHandlers[getPinNum(pin)];
    if (instance != nullptr && instance->mpExtiHandler != nullptr) {
        if (rising) {
            instance->mpExtiHandler->extiHandler(instance, IExti::Edge::RISING);
        }
        if (falling) {
            instance->mpExtiHandler->extiHandler(instance, IExti::Edge::FALLING);
        }
    }
}

void Gpio::assignPin(GpioPin_t pin)
{
    assert(IS_GPIO_PIN(pin.pin));
    mPin = pin;
}

uint32_t Gpio::getPinNum(uint32_t pin)
{
    assert(IS_GPIO_PIN(pin));

    for (uint32_t i = 0; i < skIrqsNum; i++) {
        if (pin & (1 << i)) {
            return i;
        }
    }

    assert(false);
    return 0;
}

IRQn_Type Gpio::getIrqn()
{
    /**
     * @brief   Interrupt numbers for each line.
     */
    const IRQn_Type kIRQn[skIrqsNum] = {
            EXTI0_IRQn, EXTI1_IRQn, EXTI2_IRQn, EXTI3_IRQn, EXTI4_IRQn,
            EXTI5_IRQn, EXTI6_IRQn, EXTI7_IRQn, EXTI8_IRQn,
            EXTI9_IRQn, EXTI10_IRQn, EXTI11_IRQn, EXTI12_IRQn,
            EXTI13_IRQn, EXTI14_IRQn, EXTI15_IRQn };

    return kIRQn[getPinNum(mPin.pin)];
}

void Gpio::clk(GPIO_TypeDef *port, bool isEnable)
{
#ifdef GPIOA
    if (port == GPIOA) {
        if (isEnable) {
            __HAL_RCC_GPIOA_CLK_ENABLE();
        } else {
            __HAL_RCC_GPIOA_CLK_DISABLE();
        }
    }
#endif
#ifdef GPIOB
    if (port == GPIOB) {
        if (isEnable) {
            __HAL_RCC_GPIOB_CLK_ENABLE();
        } else {
            __HAL_RCC_GPIOB_CLK_DISABLE();
        }
    }
#endif
#ifdef GPIOC
    if (port == GPIOC) {
        if (isEnable) {
            __HAL_RCC_GPIOC_CLK_ENABLE();
        } else {
            __HAL_RCC_GPIOC_CLK_DISABLE();
        }
    }
#endif
#ifdef GPIOD
    if (port == GPIOD) {
        if (isEnable) {
            __HAL_RCC_GPIOD_CLK_ENABLE();
        } else {
            __HAL_RCC_GPIOD_CLK_DISABLE();
        }
    }
#endif
#ifdef GPIOE
    if (port == GPIOE) {
        if (isEnable) {
            __HAL_RCC_GPIOE_CLK_ENABLE();
        } else {
            __HAL_RCC_GPIOE_CLK_DISABLE();
        }
    }
#endif
#ifdef GPIOF
    if (port == GPIOF) {
        if (isEnable) {
            __HAL_RCC_GPIOF_CLK_ENABLE();
        } else {
            __HAL_RCC_GPIOF_CLK_DISABLE();
        }
    }
#endif
#ifdef GPIOG
    if (port == GPIOG) {
        if (isEnable) {
            __HAL_RCC_GPIOG_CLK_ENABLE();
        } else {
            __HAL_RCC_GPIOG_CLK_DISABLE();
        }
    }
#endif
#ifdef GPIOH
    if (port == GPIOH) {
        if (isEnable) {
            __HAL_RCC_GPIOH_CLK_ENABLE();
        } else {
            __HAL_RCC_GPIOH_CLK_DISABLE();
        }
    }
#endif
#ifdef GPIOI
    if (port == GPIOI) {
        if (isEnable) {
            __HAL_RCC_GPIOI_CLK_ENABLE();
        } else {
            __HAL_RCC_GPIOI_CLK_DISABLE();
        }
    }
#endif
#ifdef GPIOJ
    if (port == GPIOJ) {
        if (isEnable) {
            __HAL_RCC_GPIOJ_CLK_ENABLE();
        } else {
            __HAL_RCC_GPIOJ_CLK_DISABLE();
        }
    }
#endif
#ifdef GPIOK
    if (port == GPIOK) {
        if (isEnable) {
            __HAL_RCC_GPIOK_CLK_ENABLE();
        } else {
            __HAL_RCC_GPIOK_CLK_DISABLE();
        }
    }
#endif
}

} /* namespace Drivers */
