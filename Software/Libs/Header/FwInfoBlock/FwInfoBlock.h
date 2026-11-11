/**
 ******************************************************************************
 * @file    FwInfoBlock.hpp
 * @brief   Firmware version format.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __FW_INFO_BLOCK_H
#define __FW_INFO_BLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

#pragma pack(push, 1)

/**
 * @brief   Shared library info block. 128 bytes.
 */
typedef struct {
    char version[32];
    char date[16];
    char time[16];
    char vendor[32];
    char reserved[32];
} FwInfoBlock_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* __FW_INFO_BLOCK_H */
