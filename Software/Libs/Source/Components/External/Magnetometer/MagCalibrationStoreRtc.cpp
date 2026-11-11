/**
 ******************************************************************************
 * @file    MagCalibrationStoreRtc.cpp
 * @brief   A magnetometer calibration kept in RTC backup registers.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "MagCalibrationStoreRtc.hpp"

#define LOG_MODULE_PRX      "MagCalStoreRtc"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Crc/Crc32.hpp"

#include <cstring>

MagCalibrationStoreRtc::MagCalibrationStoreRtc(Interface::IRtc& rtc,
        uint8_t firstReg, uint32_t frameTag)
    : mRtc(rtc)
    , mFrameTag(frameTag)
    , mFirstReg(firstReg)
    , mCrcReg(static_cast<uint8_t>(firstReg + skPayloadRegs))
{
}

bool MagCalibrationStoreRtc::save(const MagCalibration::Record& record)
{
    const uint32_t payload[skPayloadRegs] = {
        toWord(record.offsetX),
        toWord(record.offsetY),
        toWord(record.offsetZ),
        toWord(record.m00),
        toWord(record.m01),
        toWord(record.m02),
        toWord(record.m11),
        toWord(record.m12),
        toWord(record.m22),
        toWord(record.radius),
    };

    const uint32_t crc = CRC32::calculate(
            reinterpret_cast<const uint8_t*>(payload), sizeof(payload),
            mFrameTag);

    if (!writeBlock(payload, crc)) {
        return false;
    }

    LOG_INFO("Stored offset %f / %f / %f, radius %f\n",
            record.offsetX, record.offsetY, record.offsetZ, record.radius);

    return true;
}

bool MagCalibrationStoreRtc::load(MagCalibration::Record& record) const
{
    uint32_t payload[skPayloadRegs] = { 0 };

    for (uint8_t idx = 0; idx < skPayloadRegs; idx++) {
        payload[idx] = mRtc.readBackupReg(mFirstReg + idx);
    }

    const uint32_t stored = mRtc.readBackupReg(mCrcReg);
    const uint32_t actual = CRC32::calculate(
            reinterpret_cast<const uint8_t*>(payload), sizeof(payload),
            mFrameTag);

    if (stored != actual) {
        // Also a domain that lost power: all zeros, whose CRC is not zero.
        LOG_INFO("No stored calibration\n");
        return false;
    }

    MagCalibration::Record loaded;
    loaded.offsetX = toFloat(payload[0]);
    loaded.offsetY = toFloat(payload[1]);
    loaded.offsetZ = toFloat(payload[2]);
    loaded.m00     = toFloat(payload[3]);
    loaded.m01     = toFloat(payload[4]);
    loaded.m02     = toFloat(payload[5]);
    loaded.m11     = toFloat(payload[6]);
    loaded.m12     = toFloat(payload[7]);
    loaded.m22     = toFloat(payload[8]);
    loaded.radius  = toFloat(payload[9]);

    // Not stored, and deliberately not faked.
    loaded.residual = 0.0f;
    loaded.samples  = 0;

    record = loaded;

    return true;
}

bool MagCalibrationStoreRtc::clear()
{
    // Zeros under a zero CRC, which load() rejects. Read back like a write:
    // an erase that did not land returns at restart.
    const uint32_t zeros[skPayloadRegs] = { 0 };

    return writeBlock(zeros, 0U);
}

bool MagCalibrationStoreRtc::writeBlock(const uint32_t payload[skPayloadRegs],
        uint32_t crc)
{
    for (uint8_t idx = 0; idx < skPayloadRegs; idx++) {
        mRtc.writeBackupReg(mFirstReg + idx, payload[idx]);
    }

    mRtc.writeBackupReg(mCrcReg, crc);

    for (uint8_t idx = 0; idx < skPayloadRegs; idx++) {
        if (mRtc.readBackupReg(mFirstReg + idx) != payload[idx]) {
            LOG_ERROR("Backup register %u did not take the write\n",
                    static_cast<unsigned>(mFirstReg + idx));
            return false;
        }
    }

    if (mRtc.readBackupReg(mCrcReg) != crc) {
        LOG_ERROR("Backup CRC register did not take the write\n");
        return false;
    }

    return true;
}

uint32_t MagCalibrationStoreRtc::toWord(float value)
{
    uint32_t word = 0;
    std::memcpy(&word, &value, sizeof(word));
    return word;
}

float MagCalibrationStoreRtc::toFloat(uint32_t word)
{
    float value = 0.0f;
    std::memcpy(&value, &word, sizeof(value));
    return value;
}
