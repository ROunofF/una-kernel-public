/**
 ******************************************************************************
 * @file    usbd_conf.h
 * @brief   Header for usbd_conf.c file.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#ifndef __USBD_CONF_H
#define __USBD_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"

#define USBD_MAX_NUM_INTERFACES           15U
#define USBD_MAX_NUM_CONFIGURATION        1U
#define USBD_MAX_STR_DESC_SIZ             512U
#define USBD_SUPPORT_USER_STRING_DESC     1U
#define USBD_DEBUG_LEVEL                  0U
#define USBD_LPM_ENABLED                  0U
#define USBD_SELF_POWERED                 1U

#define USB_CDC_ACM_TX_FIFO                 (1024)

// #define for FS and HS identification
#define DEVICE_FS 		0
#define DEVICE_HS 		1

// Memory management macros

// Alias for memory allocation.
#define USBD_malloc         (void *)USBD_static_malloc

// Alias for memory release.
#define USBD_free           USBD_static_free

// Alias for memory set.
#define USBD_memset         memset

// Alias for memory copy.
#define USBD_memcpy         memcpy

// Alias for delay.
#define USBD_Delay          USBD_LL_Delay

// DEBUG macros
#if (USBD_DEBUG_LEVEL > 3)
#define USBD_UsrLog(...)    /*LOG_DEBUG(__VA_ARGS__); LOG_DEBUG_WP("\n");*/
#else
#define USBD_UsrLog(...)    do { } while(0)
#endif /* (USBD_DEBUG_LEVEL > 3U) */

#if (USBD_DEBUG_LEVEL > 2)
#define USBD_DbgLog(...)    /*LOG_DEBUG(__VA_ARGS__); LOG_DEBUG_WP("\n");*/
#else
#define USBD_DbgLog(...)    do { } while(0)
#endif /* (USBD_DEBUG_LEVEL > 2U) */

#if (USBD_DEBUG_LEVEL > 1)
#define USBD_ErrLog(...)    /*LOG_ERROR(__VA_ARGS__); LOG_ERROR_WP("\n");*/
#else
#define USBD_ErrLog(...)    do { } while(0)
#endif /* (USBD_DEBUG_LEVEL > 1U) */

void* USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CONF_H */
