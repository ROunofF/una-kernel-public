/**
 ******************************************************************************
 * @file    flash_diskio.cpp
 * @brief   Low level disc I/O implementation
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "flash_diskio.h"

#define LOG_MODULE_PRX      "flash_diskio"
#define LOG_MODULE_LEVEL    LOG_LEVEL_WARNING
#include "Logger.h"

#include "IFlash.hpp"

#include <assert.h>

static Interface::IFlash *mFlash = nullptr;
static uint32_t flash_diskio_get_ector_offset(uint8_t pdrv);


// Lock access to emmc from USB during read/write operation of FatFs
static bool sLock;

void flash_diskio_set_flash(Interface::IFlash &flash)
{
    mFlash = &flash;
}

bool flash_diskio_initialize(uint8_t pdrv)
{
    if (mFlash == nullptr) {
        return false;
    }

    // Initialization of the disk occurs from the outside
    switch(pdrv) {
        case FLASH_DISK_UPD:
        case FLASH_DISK_SYS:
        case FLASH_DISK_GUI:
        case FLASH_DISK_USR:
            return (mFlash->sectorGetSize() && mFlash->blockGetSize());
        default:
            break;
    }

    LOG_ERROR("Incorrect disk [%d]\n", pdrv);
    return false;
}

bool flash_diskio_status(uint8_t pdrv)
{
    if (mFlash == nullptr) {
        return false;
    }

    switch(pdrv) {
        case FLASH_DISK_UPD:
        case FLASH_DISK_SYS:
        case FLASH_DISK_GUI:
        case FLASH_DISK_USR:
            return (mFlash->sectorGetSize() && mFlash->blockGetSize());
        default:
            break;
    }

    LOG_ERROR("Incorrect disk [%d]\n", pdrv);
    return false;
}

bool flash_diskio_read(uint8_t pdrv, uint8_t *buff, uint32_t sector,
        uint32_t count)
{


    if (mFlash == nullptr || !buff || !flash_diskio_status(pdrv)) {
        return false;
    }

    uint32_t sectorOffset = 0;

    switch (pdrv) {
        case FLASH_DISK_UPD:
        case FLASH_DISK_SYS:
        case FLASH_DISK_GUI:
        case FLASH_DISK_USR:
            sectorOffset = flash_diskio_get_ector_offset(pdrv);
            break;

        default:
            LOG_ERROR("Incorrect disk [%d]\n", pdrv);
            return false;
    }

    if (sLock) {
        LOG_DEBUG("Disk [%d] locked\n", pdrv);
        return false;
    }
    sLock = true;

    bool result = true;
    uint32_t sectorSize = mFlash->sectorGetSize();

    for (uint32_t i = 0; i < count; i++) {
        uint32_t adr = (sectorOffset + sector + i) * sectorSize;

        if (mFlash->memRead(adr, buff, sectorSize) == false) {
            LOG_ERROR(" Read sector failed, disk [%u], sector %u, adr 0x%08X\n",
                    pdrv, sector, adr);
            result = false;
            break;
        }
        LOG_DEBUG(" Read sector: disk [%u], i %u, sector %u, adr 0x%08X\n", pdrv,
                i, sector, adr);

        buff += sectorSize;
    }

    sLock = false;

    return result;
}

bool flash_diskio_write(uint8_t pdrv, const uint8_t *buff, uint32_t sector,
        uint32_t count)
{
    if (mFlash == nullptr || !buff || !flash_diskio_status(pdrv)) {
        return false;
    }

    uint32_t sectorOffset = 0;

    switch (pdrv) {
        case FLASH_DISK_UPD:
        case FLASH_DISK_SYS:
        case FLASH_DISK_GUI:
        case FLASH_DISK_USR:
            sectorOffset = flash_diskio_get_ector_offset(pdrv);
            break;

        default:
            LOG_ERROR("Incorrect disk [%d]\n", pdrv);
            return false;
    }

    if (sLock) {
        LOG_DEBUG("Disk [%d] locked\n", pdrv);
        return false;
    }
    sLock = true;

    bool result = true;
    uint32_t sectorSize = mFlash->sectorGetSize();

    for (uint32_t i = 0; i < count; i++) {

#if 0   // Erase no needed for EMMC
        if (mFlash->sectorErase(sectorOffset + sector + i) == false) {
            result = false;
            break;
        }
#endif

        uint32_t adr = (sectorOffset + sector + i) * sectorSize;

        if (mFlash->memWrite(adr, buff, sectorSize) == false) {
            LOG_ERROR("Write sector failed: disk [%u], sector %u, adr 0x%08X\n",
                    pdrv, sector, adr);
            result = false;
            break;
        }

        LOG_DEBUG("Write sector: disk [%u], i %u, sector %u, adr 0x%08X\n", pdrv,
                i, sector, adr);

        buff += sectorSize;
    }

    sLock = false;

    return result;
}

bool flash_diskio_erase(uint8_t pdrv, uint32_t sector, uint32_t count)
{
    if (mFlash == nullptr || !flash_diskio_status(pdrv)) {
        return false;
    }

    uint32_t sectorOffset = 0;

    switch (pdrv) {
        case FLASH_DISK_UPD:
        case FLASH_DISK_SYS:
        case FLASH_DISK_GUI:
        case FLASH_DISK_USR:
            sectorOffset = flash_diskio_get_ector_offset(pdrv);
            break;

        default:
            LOG_ERROR("Incorrect disk [%d]\n", pdrv);
            return false;
    }

    if (sLock) {
        LOG_DEBUG("Disk [%d] locked\n", pdrv);
        return false;
    }
    sLock = true;

    bool result = true;

    for (uint32_t i = 0; i < count; i++) {
        if (mFlash->sectorErase(sectorOffset + sector + i) == false) {
            LOG_ERROR("Erase sector failed: disk [%u], sector %u\n", pdrv,
                    sector);
            result = false;
            break;
        }

        LOG_DEBUG("Erase sector: disk [%u], i %u, sector %u\n", pdrv, i,
                sector);
    }

    sLock = false;

    return result;
}

bool flash_diskio_block_size(uint8_t pdrv, uint32_t *block_size)
{
    if (mFlash == nullptr || !block_size || !flash_diskio_status(pdrv)) {
        return false;
    }

    // Retrieves erase block size in unit of sector of the flash
    *block_size = mFlash->blockGetSize() / mFlash->sectorGetSize();

    return true;
}

bool flash_diskio_sector_size(uint8_t pdrv, uint32_t *sector_size)
{
    if (mFlash == nullptr || !sector_size || !flash_diskio_status(pdrv)) {
        return false;
    }

    // Get sector size
    *sector_size = mFlash->sectorGetSize();

    return true;
}

bool flash_diskio_sector_count(uint8_t pdrv, uint32_t *sector_count)
{
    if (mFlash == nullptr || !sector_count || !flash_diskio_status(pdrv)) {
        return false;
    }

    if((FLASH_DISK_UPD_SZ_SECTORS
                + FLASH_DISK_SYS_SZ_SECTORS
                + FLASH_DISK_GUI_SZ_SECTORS) > mFlash->sectorGetCount())
    {
         return false;
    }

    // Get media size
    switch (pdrv) {
        case FLASH_DISK_UPD: {
            *sector_count = FLASH_DISK_UPD_SZ_SECTORS;
        }
            break;

        case FLASH_DISK_SYS: {
            *sector_count = FLASH_DISK_SYS_SZ_SECTORS;
        }
            break;

        case FLASH_DISK_GUI: {
            *sector_count = FLASH_DISK_GUI_SZ_SECTORS;
        }
            break;

        case FLASH_DISK_USR: {
            *sector_count = mFlash->sectorGetCount()
                    - (FLASH_DISK_UPD_SZ_SECTORS + FLASH_DISK_SYS_SZ_SECTORS
                       + FLASH_DISK_GUI_SZ_SECTORS);
        }
            break;

        default:
            LOG_ERROR("Incorrect disk [%d]\n", pdrv);
            return false;
    }
    LOG_DEBUG("Sector count: disk [%u], count %u\n", pdrv, *sector_count);

    return true;
}

static uint32_t flash_diskio_get_ector_offset(uint8_t pdrv)
{
    switch (pdrv) {
        case FLASH_DISK_UPD:
            return 0;

        case FLASH_DISK_SYS:
            return  FLASH_DISK_UPD_SZ_SECTORS;

        case FLASH_DISK_GUI:
            return (FLASH_DISK_UPD_SZ_SECTORS + FLASH_DISK_SYS_SZ_SECTORS);


        case FLASH_DISK_USR: {
            return (FLASH_DISK_UPD_SZ_SECTORS + FLASH_DISK_SYS_SZ_SECTORS
                           + FLASH_DISK_GUI_SZ_SECTORS);
        }

        default:
            assert(false);
            break;
    }

    return 0;
}
