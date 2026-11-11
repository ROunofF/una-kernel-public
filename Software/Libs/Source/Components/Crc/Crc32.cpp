/**
 ******************************************************************************
 * @file    Crc32.cpp
 * @brief   The Crc32 class description.
 *          Class is used to calculate and check CRC32.
 ******************************************************************************
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "Crc/Crc32.hpp"

/* Public methods implementation begin ---------------------------------------*/
/**
 * @brief  Count CRC32.
 * @param  data: the pointer to the array with data to count CRC32.
 * @param  length: the length of data.
 * @param  crc: the variable where to save counted CRC32.
 * @retval calculated crc.
 */
uint32_t CRC32::calculate(const uint8_t* data, uint32_t length, uint32_t prevCrc)
{
    uint32_t crc = ~prevCrc;

    if (data != nullptr) {
        for (uint32_t i = 0; i < length; i++) {
            crc = crc ^ data[i];
            for (uint32_t j = 8; j > 0; j--) {
                crc = (crc >> 1) ^ (0xEDB88320 & ((crc & 1) ? 0xFFFFFFFF : 0));
            }
        }
    }

    return ~crc;
}

/**
 * @brief  Check if CRC is in compliance with the data.
 * @param  data: the pointer to the array with data to check.
 * @param  length: the length of data to check.
 * @param  crc: the CRC to compare with.
 * @retval true: in the case of CRC is in compliance with the data.
 */
bool CRC32::check(const uint8_t* data, uint32_t length, uint32_t crc)
{
    return (CRC32::calculate(data, length, 0) == crc);
}
