/**
 ******************************************************************************
 * @file    exfat_helper.c
 * @brief   Implementation of helper functions for exFAT filesystem operations.
 ******************************************************************************
 */

#include "exfat_helper.h"
#include <string.h>
#include <assert.h>
#include "diskio.h"


/**
 * @brief Convert ASCII string to UTF-16LE encoding
 */
static void ascii_to_utf16le(const char *src, uint8_t *dst, uint8_t *out_len)
{
    uint8_t i = 0;

    while (*src && i < 11) {
        unsigned char c = (unsigned char)*src++;
        dst[i * 2] = c;
        dst[i * 2 + 1] = 0x00;
        i++;
    }

    *out_len = i;
}

/**
 * @brief Convert cluster number to logical sector address
 */
static uint32_t cluster_to_sector(uint32_t cluster, FATFS* fs)
{
    return fs->database + (cluster - 2) * fs->csize;
}

/**
 * @brief Create exFAT volume label
 */
bool exfat_create_volume_label(uint8_t vol, FATFS *fs, const char *name)
{
    BYTE  sector[512];
    DWORD sector_size;

    if (disk_ioctl(vol, GET_SECTOR_SIZE, &sector_size) != RES_OK) {
        return false;
    }

    if (sector_size != sizeof(sector)) {
        assert(0);
        return false;
    }

    uint8_t label_len = 0;
    uint8_t label_utf16[22];

    ascii_to_utf16le(name, label_utf16, &label_len);

    if (label_len == 0) {
        return false;
    }

    uint32_t first_sector = cluster_to_sector(fs->dirbase, fs);

    if (disk_read(vol, sector, first_sector, 1) != RES_OK) {
        return false;
    }

    memset(sector, 0, 32);

    sector[0] = 0x83;
    sector[1] = label_len;
    memcpy(sector + 2, label_utf16, 2 * label_len);

    return (disk_write(vol, sector, first_sector, 1) == RES_OK);
}
