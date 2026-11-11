/**
 ******************************************************************************
 * @file    Crc32.hpp
 * @brief   Header file for Crc32 class.
 ******************************************************************************
 *
 ******************************************************************************
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __TOOLS_CRC32_HPP
#define __TOOLS_CRC32_HPP

/* Includes ------------------------------------------------------------------*/
#include "Porting.h"

/**
 * @brief      Class is used to calculate and check CRC32.
 * @note       None
 * @bug        None
 */
class CRC32
{
public:
    static uint32_t calculate(const uint8_t* data,
                              uint32_t       length,
                              uint32_t       crc = 0);

    static bool check(const uint8_t* data,
                      uint32_t       length,
                      uint32_t       crc);
};

#endif // __PROTOCOLS_CRC32_HPP
