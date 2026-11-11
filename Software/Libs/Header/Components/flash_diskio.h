/**
 ******************************************************************************
 * @file    flash_diskio.h
 * @brief   Low level disk I/O interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __FLASH_DISK_IO_H
#define __FLASH_DISK_IO_H

#ifdef __cplusplus
#include "IFlash.hpp"
void flash_diskio_set_flash(Interface::IFlash &flash);
extern "C" {
#endif

#include "stdint.h"
#include "stdbool.h"

#define FLASH_DISK_MB2SECTORS(MB) ((MB * (1 << 20)) / 512)

// Definitions of physical drive number for each drive
// NOTE:
#define FLASH_DISK_UPD   (0)    // Update files. Used by FatFS
#define FLASH_DISK_SYS   (1)    // System resources. Used by FatFS
#define FLASH_DISK_GUI   (2)    // GUI resources. Not used by FatFS / MSC.
#define FLASH_DISK_USR   (3)    // User files. Used by FatFS / MSC.

#define FLASH_DISK_UPD_SZ_SECTORS (FLASH_DISK_MB2SECTORS(100))  // 100 MB @512b per sector
#define FLASH_DISK_GUI_SZ_SECTORS (FLASH_DISK_MB2SECTORS(100))  // 100 MB @512b per sector
#define FLASH_DISK_SYS_SZ_SECTORS (FLASH_DISK_MB2SECTORS(100))  // 100 MB @512b per sector
//#define FLASH_DISK_USR_SZ_SECTORS (FLASH_DISK_MB2SECTORS())   // Remaining space


bool flash_diskio_initialize(uint8_t pdrv);
bool flash_diskio_status(uint8_t pdrv);
bool flash_diskio_read(uint8_t pdrv, uint8_t *buff, uint32_t sector, uint32_t count);
bool flash_diskio_write(uint8_t pdrv, const uint8_t *buff, uint32_t sector, uint32_t count);
bool flash_diskio_erase(uint8_t pdrv, uint32_t sector, uint32_t count);
bool flash_diskio_block_size(uint8_t pdrv, uint32_t *block_size);
bool flash_diskio_sector_size(uint8_t pdrv, uint32_t *sector_size);
bool flash_diskio_sector_count(uint8_t pdrv, uint32_t *sector_count);

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_DISK_IO_H */

