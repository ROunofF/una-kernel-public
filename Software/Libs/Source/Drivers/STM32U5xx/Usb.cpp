/**
 ******************************************************************************
 * @file    Usb.cpp
 * @brief   Low level USB initialization.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Usb.h"
#include "System.hpp"

#if (USE_SYSTEM_SLEEP == 1)
#include "Sleep.hpp"
#endif

static PCD_HandleTypeDef hpcd;

// Upper layer BCD result callback - receives DriverUsb_PortType_t only.
static DriverUsb_BcdCallback_t sBcdCallback = nullptr;

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/**
 * @brief   Check PHY enabled state directly from RCC register.
 *          More reliable than a software flag - reflects real hardware state.
 * @retval  true if USB OTG HS clock is enabled.
 */
static bool isPhyEnabled()
{
    return __HAL_RCC_USB_OTG_HS_IS_CLK_ENABLED() != 0U;
}

/**
 * @brief   Map HAL BCD event to DriverUsb_PortType_t.
 *          DISCOVERY_COMPLETED is not mapped here - it triggers the callback
 *          with the last detected port type instead.
 * @param   event: HAL BCD event.
 * @retval  Corresponding DriverUsb_PortType_t or NONE if not a type event.
 */
static DriverUsb_PortType_t BcdEventToPortType(PCD_BCD_MsgTypeDef event)
{
    switch (event) {
        case PCD_BCD_STD_DOWNSTREAM_PORT:      return DRIVER_USB_PORT_SDP;
        case PCD_BCD_CHARGING_DOWNSTREAM_PORT: return DRIVER_USB_PORT_CDP;
        case PCD_BCD_DEDICATED_CHARGING_PORT:  return DRIVER_USB_PORT_DCP;
        default:                               return DRIVER_USB_PORT_NONE;
    }
}

/**
 * @brief   Release D+/D- GPIO lines with pull-down.
 *          Called from DriverUsb_PhyDisable() to avoid floating state.
 */
static void UsbGpio_DeInit()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = USB_D_P_Pin | USB_D_N_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(USB_D_P_GPIO_Port, &GPIO_InitStruct);
}

/* --------------------------------------------------------------------------
 * Internal HAL BCD callback - registered in hpcd.BCDCallback.
 * Translates HAL events to DriverUsb_PortType_t and forwards to upper layer.
 * DISCOVERY_COMPLETED triggers the callback with the accumulated port type.
 * -------------------------------------------------------------------------- */

static DriverUsb_PortType_t sBcdPortType = DRIVER_USB_PORT_NONE;

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void BcdHalCallback(PCD_HandleTypeDef *pcdHandle, PCD_BCD_MsgTypeDef event)
#else
void HAL_PCDEx_BCD_Callback(PCD_HandleTypeDef *pcdHandle, PCD_BCD_MsgTypeDef event)
#endif
{
    (void)pcdHandle;

    if (event == PCD_BCD_DISCOVERY_COMPLETED) {
        // Detection finished - deliver result to upper layer
        if (sBcdCallback != nullptr) {
            sBcdCallback(sBcdPortType);
        }
        sBcdPortType = DRIVER_USB_PORT_NONE;
        return;
    }

    // Intermediate events - accumulate port type, ignore contact/unknown
    DriverUsb_PortType_t type = BcdEventToPortType(event);
    if (type != DRIVER_USB_PORT_NONE) {
        sBcdPortType = type;
    }
}

/* --------------------------------------------------------------------------
 * MSP Init / DeInit - minimal, only IRQ.
 * PHY / clocks / VddUSB are managed by PhyEnable() / PhyDisable().
 * -------------------------------------------------------------------------- */

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_MspInit(PCD_HandleTypeDef *pcdHandle)
#else
void HAL_PCD_MspInit(PCD_HandleTypeDef *pcdHandle)
#endif
{
    if (pcdHandle->Instance == USB_OTG_HS) {
        HAL_NVIC_SetPriority(OTG_HS_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
    }
}

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
static void PCD_MspDeInit(PCD_HandleTypeDef *pcdHandle)
#else
void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcdHandle)
#endif
{
    if (pcdHandle->Instance == USB_OTG_HS) {
        HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
    }
}

/* --------------------------------------------------------------------------
 * BCD callback registration
 * -------------------------------------------------------------------------- */

void DriverUsb_SetBcdCallback(DriverUsb_BcdCallback_t cb)
{
    sBcdCallback = cb;
}

/* --------------------------------------------------------------------------
 * PHY Enable / Disable
 * -------------------------------------------------------------------------- */

bool DriverUsb_IsPhyEnabled()
{
    return isPhyEnabled();
}

void DriverUsb_PhyEnable()
{
    if (isPhyEnabled()) {
        return;
    }

    // Request HSE - required source for USB OTG HS and USBPHYC clocks
    DriverUsb_EnableCLK();

    __HAL_RCC_SYSCFG_CLK_ENABLE();

    // Set PHY reference clock source to HSE
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USBPHY;
    PeriphClkInit.UsbPhyClockSelection = RCC_USBPHYCLKSOURCE_HSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
        Error_Handler();
    }

    HAL_SYSCFG_SetOTGPHYReferenceClockSelection(SYSCFG_OTG_HS_PHY_CLK_SELECT_1);

    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
    __HAL_RCC_USBPHYC_CLK_ENABLE();

    if (__HAL_RCC_PWR_IS_CLK_DISABLED()) {
        __HAL_RCC_PWR_CLK_ENABLE();
        HAL_PWREx_EnableVddUSB();
        HAL_PWREx_EnableUSBHSTranceiverSupply();
        __HAL_RCC_PWR_CLK_DISABLE();
    } else {
        HAL_PWREx_EnableVddUSB();
        HAL_PWREx_EnableUSBHSTranceiverSupply();
    }

    HAL_SYSCFG_EnableOTGPHY(SYSCFG_OTG_HS_PHY_ENABLE);

#if (USE_SYSTEM_SLEEP == 1)
    Driver::Sleep::Pause();
#endif
}

void DriverUsb_PhyDisable()
{
    if (!isPhyEnabled()) {
        return;
    }

    // Put PHY into reset state before removing clocks
    HAL_SYSCFG_EnableOTGPHY(SYSCFG_OTG_HS_PHY_UNDERRESET);

    if (__HAL_RCC_PWR_IS_CLK_DISABLED()) {
        __HAL_RCC_PWR_CLK_ENABLE();
        HAL_PWREx_DisableUSBHSTranceiverSupply();
        HAL_PWREx_DisableVddUSB();
        __HAL_RCC_PWR_CLK_DISABLE();
    } else {
        HAL_PWREx_DisableUSBHSTranceiverSupply();
        HAL_PWREx_DisableVddUSB();
    }

    // Remove clocks only after PHY and transceiver are off
    __HAL_RCC_USBPHYC_CLK_DISABLE();
    __HAL_RCC_USB_OTG_HS_CLK_DISABLE();

    // Pull D+/D- to GND - prevents floating and blocks accidental QC trigger
    UsbGpio_DeInit();

    // Release HSE request
    DriverUsb_DisableCLK();

#if (USE_SYSTEM_SLEEP == 1)
    Driver::Sleep::Resume();
#endif
}

/* --------------------------------------------------------------------------
 * BCD detection
 * -------------------------------------------------------------------------- */

void DriverUsb_VBUSDetect()
{
    HAL_PCDEx_BCD_VBUSDetect(&hpcd);
}

/* --------------------------------------------------------------------------
 * PCD peripheral
 * -------------------------------------------------------------------------- */

PCD_HandleTypeDef* DriverUsb_GetHandle()
{
    return &hpcd;
}

HAL_StatusTypeDef DriverUsb_PCD_Init(DriverUsb_Speed_t speed)
{
#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
    hpcd.MspInitCallback   = PCD_MspInit;
    hpcd.MspDeInitCallback = PCD_MspDeInit;
#endif

    hpcd.Instance                 = USB_OTG_HS;
    hpcd.Init.dev_endpoints       = 6;
    hpcd.Init.phy_itface          = USB_OTG_HS_EMBEDDED_PHY;
    hpcd.Init.Sof_enable          = DISABLE;
    hpcd.Init.low_power_enable    = DISABLE;
    hpcd.Init.lpm_enable          = DISABLE;
    hpcd.Init.use_dedicated_ep1   = DISABLE;
    hpcd.Init.vbus_sensing_enable = DISABLE;
    hpcd.Init.dma_enable          = DISABLE;

    hpcd.Init.speed = (speed == DRIVER_USB_HS) ? PCD_SPEED_HIGH : PCD_SPEED_FULL;

    HAL_StatusTypeDef ret = HAL_PCD_Init(&hpcd);

#if (USE_HAL_PCD_REGISTER_CALLBACKS == 1U)
    // Register BCD callback after HAL_PCD_Init - it resets the handle internals
    if (ret == HAL_OK) {
        hpcd.BCDCallback = BcdHalCallback;
    }
#endif

    return ret;
}

HAL_StatusTypeDef DriverUsb_PCD_DeInit()
{
    return HAL_PCD_DeInit(&hpcd);
}

/* --------------------------------------------------------------------------
 * Misc
 * -------------------------------------------------------------------------- */

void DriverUsb_IrqHandler()
{
    HAL_PCD_IRQHandler(&hpcd);
}

void DriverUsb_Delay(uint32_t ms)
{
    Driver::System::Delay(ms);
}

uint32_t DriverUsb_GetTime()
{
    return Driver::System::GetTimeMs();
}

void DriverUsb_EnableCLK()
{
    Driver::System::RequestHSE(true);
}

void DriverUsb_DisableCLK()
{
    Driver::System::RequestHSE(false);
}
