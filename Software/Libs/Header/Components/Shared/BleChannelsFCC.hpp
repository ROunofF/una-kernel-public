/**
 ******************************************************************************
 * @file    BleChannelsFCC.hpp
 * @brief   BLE channels list for FCC testing.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __BLE_CHANNELS_FCC_HPP
#define __BLE_CHANNELS_FCC_HPP

#include <cstdint>
#include <array>

/**
 * @brief BLE channels list.
 */
struct BleChannelsFCC {

    struct Info {
        uint8_t number;
        uint32_t frequency;
    };

    static constexpr std::array<Info, 3> list {{
        {0, 2402}, {19, 2440}, {39, 2480}
    }};

};

#endif // __BLE_CHANNELS_FCC_HPP
