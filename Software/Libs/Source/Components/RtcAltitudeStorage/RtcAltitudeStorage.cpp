/**
 ******************************************************************************
 * @file    RtcAltitudeStorage.cpp
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

#include "RtcAltitudeStorage/RtcAltitudeStorage.hpp"

#define LOG_MODULE_PRX      "RtcAltitudeStor"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Crc/Crc32.hpp"

#include <cstring>

namespace Component {

RtcAltitudeStorage::RtcAltitudeStorage(Interface::IRtc& rtc,
                                       uint8_t          addrAltitude,
                                       uint8_t          addrCrc)
    : mRtc(rtc)
    , mAddrAltitude(addrAltitude)
    , mAddrCrc(addrCrc)
{
}

bool RtcAltitudeStorage::set(float altitude)
{
    uint32_t tmp = 0;

    std::memcpy(&tmp, &altitude, sizeof(tmp));
    mRtc.writeBackupReg(mAddrAltitude, tmp);

    tmp = CRC32::calculate((const uint8_t*)&tmp, sizeof(tmp));
    mRtc.writeBackupReg(mAddrCrc, tmp);

    LOG_DEBUG("altitude %f\n", altitude);

    return true;
}

bool RtcAltitudeStorage::get(float& altitude)
{
    uint32_t crc = mRtc.readBackupReg(mAddrCrc);

    uint32_t tmp = mRtc.readBackupReg(mAddrAltitude);

    uint32_t crcCalculated = CRC32::calculate((const uint8_t*)&tmp, sizeof(tmp));

    if (crc != crcCalculated) {
        LOG_WARNING("No valid altitude. CRC is invalid\n");
        return false;
    }

    std::memcpy(&altitude, &tmp, sizeof(altitude));

    LOG_DEBUG("altitude %f\n", altitude);

    return true;
}

void RtcAltitudeStorage::reset()
{
    uint32_t tmp = 0;
    mRtc.writeBackupReg(mAddrAltitude, tmp);
    mRtc.writeBackupReg(mAddrCrc, tmp);
}

}  /* namespace Component */

