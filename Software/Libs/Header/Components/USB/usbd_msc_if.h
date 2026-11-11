/**
 ******************************************************************************
 * @file    usbd_msc_if.h
 * @brief   USB STORAGE Interface.
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

#ifndef __USBD_MSC_IF_H
#define __USBD_MSC_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_msc.h"

typedef struct
{
  USBD_StatusTypeDef (*status)(uint8_t drv);
  USBD_StatusTypeDef (*read)(uint8_t drv, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
  USBD_StatusTypeDef (*write)(uint8_t drv, const uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
  USBD_StatusTypeDef (*getBlockSize)(uint8_t drv, uint32_t *block_size);
  USBD_StatusTypeDef (*getBlockNum)(uint8_t drv, uint32_t *block_num);
} USBD_DiskIO;

/**
 * @brief   Configure MSC driver.
 * @param   drv: USB drive index.
 * @param   ioif: input/output interface
 */
void USBD_STORAGE_config(uint8_t drv, USBD_DiskIO ioif);

/**
 * @brief   Get STORAGE Interface callbacks.
 * @retval  STORAGE Interface callbacks structure.
 */
USBD_StorageTypeDef USBD_STORAGE_getInterface();

#ifdef __cplusplus
}
#endif

#endif /* __USBD_MSC_IF_H */
