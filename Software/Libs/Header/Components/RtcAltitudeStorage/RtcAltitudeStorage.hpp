/**
 ******************************************************************************
 * @file    RtcAltitudeStorage.hpp
 * @brief   Stores and retrieves the phone-provided altitude value in RTC backup
 *          registers with CRC validation.
 ******************************************************************************
 *
 * @details
 * The altitude value (float) is serialized into a 32-bit word and stored in the
 * RTC backup domain at the configured register address. A CRC32 is computed for
 * the stored value and saved to a separate backup register. On read, the value
 * is returned only if it is non-zero and the CRC matches.
 *
 ******************************************************************************
 */


#pragma once

#include "Porting.h"
#include "IRtc.hpp"

namespace Component {

class RtcAltitudeStorage
{
public:
    RtcAltitudeStorage(Interface::IRtc& rtc,
                       uint8_t          addrAltitude,
                       uint8_t          addrCrc);

    bool set(float alt);
    bool get(float& alt);
    void reset();

private:
    Interface::IRtc& mRtc;
    const uint8_t    mAddrAltitude;
    const uint8_t    mAddrCrc;
};

}  /* namespace Component */

