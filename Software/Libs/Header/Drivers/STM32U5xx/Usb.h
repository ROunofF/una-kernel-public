/**
 ******************************************************************************
 * @file    Usb.h
 * @brief   Low level USB initialization.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __DRIVER_USB_H
#define __DRIVER_USB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>

/* --------------------------------------------------------------------------
 * Speed selection
 * -------------------------------------------------------------------------- */

typedef enum {
    DRIVER_USB_FS = 0,
    DRIVER_USB_HS = 1,
} DriverUsb_Speed_t;

/* --------------------------------------------------------------------------
 * Port type - result of BCD detection.
 * All HAL BCD types are mapped internally; upper layers never see HAL types.
 * -------------------------------------------------------------------------- */

typedef enum {
    DRIVER_USB_PORT_NONE  = 0,  /**< No port detected, BCD timeout or error   */
    DRIVER_USB_PORT_SDP,        /**< Standard Downstream Port  (PC)           */
    DRIVER_USB_PORT_CDP,        /**< Charging Downstream Port  (PC + Charge)  */
    DRIVER_USB_PORT_DCP,        /**< Dedicated Charging Port   (Charger only) */
} DriverUsb_PortType_t;

/* --------------------------------------------------------------------------
 * BCD result callback.
 * Delivered after DriverUsb_VBUSDetect() returns, in the same task context.
 * -------------------------------------------------------------------------- */

typedef void (*DriverUsb_BcdCallback_t)(DriverUsb_PortType_t portType);

/**
 * @brief   Register BCD result callback.
 *          Must be called before DriverUsb_VBUSDetect().
 * @param   cb: Callback function pointer. Pass NULL to unregister.
 */
void DriverUsb_SetBcdCallback(DriverUsb_BcdCallback_t cb);

/* --------------------------------------------------------------------------
 * PHY control
 * -------------------------------------------------------------------------- */

/**
 * @brief   Check if USB PHY is currently enabled.
 *          Reads directly from RCC register - reflects real hardware state.
 * @retval  true if PHY is enabled, false otherwise.
 */
bool DriverUsb_IsPhyEnabled();

/**
 * @brief   Enable USB PHY, clocks, VddUSB and transceiver.
 *          Must be called before DriverUsb_VBUSDetect() or USBD_Start().
 *          Idempotent - safe to call if already enabled.
 */
void DriverUsb_PhyEnable();

/**
 * @brief   Disable USB PHY, transceiver, VddUSB and clocks.
 *          Releases D+/D- lines with pull-down to avoid floating state.
 *          Idempotent - safe to call if already disabled.
 */
void DriverUsb_PhyDisable();

/* --------------------------------------------------------------------------
 * BCD detection
 * -------------------------------------------------------------------------- */

/**
 * @brief   Run blocking BCD (Battery Charging Detection) sequence.
 *          Takes approximately 500ms to complete.
 *          Result is delivered via callback registered with
 *          DriverUsb_SetBcdCallback(). Requires DriverUsb_PhyEnable() first.
 */
void DriverUsb_VBUSDetect();

/* --------------------------------------------------------------------------
 * PCD peripheral
 * -------------------------------------------------------------------------- */

/**
 * @brief   Get PCD peripheral handle.
 * @retval  Pointer to internal PCD handle.
 */
PCD_HandleTypeDef* DriverUsb_GetHandle();

/**
 * @brief   Initialize PCD peripheral.
 * @param   speed: Speed selection.
 * @retval  HAL status.
 */
HAL_StatusTypeDef DriverUsb_PCD_Init(DriverUsb_Speed_t speed);

/**
 * @brief   De-initialize PCD peripheral.
 * @retval  HAL status.
 */
HAL_StatusTypeDef DriverUsb_PCD_DeInit();

/* --------------------------------------------------------------------------
 * Misc
 * -------------------------------------------------------------------------- */

/**
 * @brief   USB OTG HS IRQ handler. Call from stm32u5xx_it.cpp.
 */
void DriverUsb_IrqHandler();

/**
 * @brief   Delay helper used by USB device library.
 * @param   ms: Delay in milliseconds.
 */
void DriverUsb_Delay(uint32_t ms);

/**
 * @brief   Get current system time in milliseconds.
 * @retval  Time in ms.
 */
uint32_t DriverUsb_GetTime();

/**
 * @brief   Request HSE clock enable for USB.
 */
void DriverUsb_EnableCLK();

/**
 * @brief   Request HSE clock disable for USB.
 */
void DriverUsb_DisableCLK();

#ifdef __cplusplus
}
#endif

#endif /* __DRIVER_USB_H */
