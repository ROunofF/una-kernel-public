/**
 ******************************************************************************
 * @file    usbd_msc_if.c
 * @brief   USB MSC Interface.
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

#include "usbd_msc_if.h"



#define LOG_MODULE_PRX      "usbd_msc_if"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#define STORAGE_LUN_NBR     (1)


/**
 *  USB Mass storage Standard Inquiry Data.
 */
const int8_t STORAGE_Inquirydata[] = {/* 36 */
  
  /* LUN 0 */
  0x00,
  0x80,
  0x02,
  0x02,
  (STANDARD_INQUIRY_DATA_LEN - 5),
  0x00,
  0x00,	
  0x00,
  'S', 'T', 'M', ' ', ' ', ' ', ' ', ' ', /* Manufacturer : 8 bytes */
  'P', 'r', 'o', 'd', 'u', 'c', 't', ' ', /* Product      : 16 Bytes */
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  '0', '.', '0' ,'1'                      /* Version      : 4 Bytes */
}; 

static int8_t STORAGE_Init(uint8_t lun);
static int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num, uint16_t *block_size);
static int8_t STORAGE_IsReady(uint8_t lun);
static int8_t STORAGE_IsWriteProtected(uint8_t lun);
static int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t STORAGE_GetMaxLun(void);

static USBD_DiskIO sIO;
static uint8_t sDrv[STORAGE_LUN_NBR];

void USBD_STORAGE_config(uint8_t drv, USBD_DiskIO ioif)
{
    sIO = ioif;
    sDrv[0] = drv;
}

USBD_StorageTypeDef USBD_STORAGE_getInterface()
{
    USBD_StorageTypeDef fops = { 0 };

    fops.Init = &STORAGE_Init;
    fops.GetCapacity = &STORAGE_GetCapacity;
    fops.IsReady = &STORAGE_IsReady;
    fops.IsWriteProtected = &STORAGE_IsWriteProtected;
    fops.Read = &STORAGE_Read;
    fops.Write = &STORAGE_Write;
    fops.GetMaxLun = &STORAGE_GetMaxLun;
    fops.pInquiry = (int8_t*) STORAGE_Inquirydata;

    return fops;
}

/**
 * @brief   Initializes the storage unit (medium).
 * @param   lun: Logical unit number.
 * @retval  USBD_OK if all operations are OK else USBD_FAIL
 */
int8_t STORAGE_Init(uint8_t lun)
{
    UNUSED(lun);
    return USBD_OK;
}

/**
 * @brief   Returns the medium capacity.
 * @param   lun: Logical unit number.
 * @param   block_num: Number of total block number.
 * @param   block_size: Block size.
 * @retval  USBD_OK if all operations are OK else USBD_FAIL
 */
int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num,
        uint16_t *block_size)
{
    lun = 0;    // Support only 1 disk

    if (!block_size || !block_num ) {
        return USBD_FAIL;
    }

    *block_size = 0;
    *block_num = 0;

    if (STORAGE_IsReady(sDrv[lun]) != USBD_OK) {
        return USBD_FAIL;
    }

    if (!sIO.getBlockSize || !sIO.getBlockNum) {
        return USBD_FAIL;
    }

    uint32_t blsz = 0;
    uint32_t blnum = 0;
    if (sIO.getBlockSize(sDrv[lun], &blsz) != USBD_OK ||
            sIO.getBlockNum(sDrv[lun], &blnum) != USBD_OK) {
        return USBD_FAIL;
    }

    *block_size = (uint16_t)blsz;
    *block_num = blnum;

    return USBD_OK;
}

/**
 * @brief   Checks whether the medium is ready.
 * @param   lun: Logical unit number.
 * @retval  USBD_OK if all operations are OK else USBD_FAIL
 */
int8_t STORAGE_IsReady(uint8_t lun)
{
    lun = 0;    // Support only 1 disk
    if (!sIO.status) {
        return USBD_FAIL;
    }

    return sIO.status(sDrv[lun]);
}

/**
 * @brief  Checks whether the medium is write protected.
 * @param  lun: Logical unit number.
 * @retval USBD_OK if all operations are OK else USBD_FAIL
 */
int8_t STORAGE_IsWriteProtected(uint8_t lun)
{
    return USBD_OK;
}

/**
 * @brief  Reads data from the medium.
 * @param  lun: Logical unit number.
 * @param  buf: data buffer.
 * @param  blk_addr: Logical block address.
 * @param  blk_len: Blocks number.
 * @retval USBD_OK if all operations are OK else USBD_FAIL
 */
int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    lun = 0;    // Support only 1 disk
    if (!sIO.read) {
        return USBD_FAIL;
    }

    return sIO.read(sDrv[lun], buf, blk_addr, blk_len);
}

/**
 * @brief  Writes data into the medium.
 * @param  lun: Logical unit number.
 * @param  buf: data buffer.
 * @param  blk_addr: Logical block address.
 * @param  blk_len: Blocks number.
 * @retval USBD_OK if all operations are OK else USBD_FAIL
 */
int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    lun = 0;    // Support only 1 disk
    if (!sIO.write) {
        return USBD_FAIL;
    }

    return sIO.write(sDrv[lun], (const uint8_t*) buf, blk_addr, blk_len);
}

/**
 * @brief  Returns the Max Supported LUNs.
 * @param  None
 * @retval Lun(s) number.
 */
int8_t STORAGE_GetMaxLun(void)
{
    return (STORAGE_LUN_NBR - 1);
}

