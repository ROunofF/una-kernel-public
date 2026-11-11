/**
 ******************************************************************************
 * @file    usbd_cdc_acm_if.h
 * @brief   USB CDC Interface.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#ifndef __USBD_CDC_ACM_IF_H
#define __USBD_CDC_ACM_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "usbd_cdc_acm.h"

/**
 * @brief   Port open/close callback.
 * @param   ch: VCP CDC channel
 * @param   state: Port status. 'true' - opened, 'false' - closed.
 */
typedef void (*USBD_CDC_PortOpenCb_t)(uint8_t ch, bool state);

/**
 * @brief   Receive callback.
 * @param   ch: VCP CDC channel
 * @param   buf: Buffer of data to be received
 * @param   len: Number of data received (in bytes)
 */
typedef void (*USBD_CDC_RxCb_t)(uint8_t ch, uint8_t *buf, uint32_t len);

/**
 * @brief   Configure CDC driver.
 * @param   pDev: USB Device handle.
 * @param   portCb: Port open/close callback
 * @param   rxCb: Receive callback.
 */
void USBD_CDC_config(USBD_HandleTypeDef *pDev, USBD_CDC_PortOpenCb_t portCb,
        USBD_CDC_RxCb_t rxCb);

/**
 * @brief   Get CDC Interface callbacks.
 * @retval  CDC Interface callbacks structure.
 */
USBD_CDC_ACM_ItfTypeDef USBD_CDC_getInterface();

/**
 * @brief   Transmit data.
 * @param   ch: VCP CDC channel
 * @param   pBuf: Buffer of data to be sent
 * @param   len: Number of data to be sent (in bytes)
 * @retval  Execution status. 'true' if success, 'false' otherwise.
 */
bool USBD_CDC_Transmit(uint8_t ch, const uint8_t* pBuf, uint32_t len);

/**
 * @brief   Check if transaction ongoing.
 * @param   ch: VCP CDC channel
 * @retval  Status. 'true' - transaction ongoing, 'false' - otherwise.
 */
bool USBD_CDC_isTxBusy(uint8_t ch);

/**
 * @brief   Check if serial port is opened.
 * @param   ch: VCP CDC channel
 * @retval  'true' - port opened, 'false' - closed
 */
bool USBD_CDC_isPortOpen(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CDC_ACM_IF_H */
