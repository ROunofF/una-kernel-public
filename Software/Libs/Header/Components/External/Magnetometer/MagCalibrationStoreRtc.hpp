/**
 ******************************************************************************
 * @file    MagCalibrationStoreRtc.hpp
 * @brief   A magnetometer calibration kept in RTC backup registers.
 *
 *          Battery-backed: survives reset, STOP2 and power-off, but not a
 *          deep discharge or battery disconnect. Ten payload registers and a
 *          CRC32 seeded with the frame tag. No version field: a layout or
 *          frame change fails the CRC and the watch asks to recalibrate.
 ******************************************************************************
 */

#ifndef COMPONENT_MAG_CALIBRATION_STORE_RTC_HPP
#define COMPONENT_MAG_CALIBRATION_STORE_RTC_HPP

#include "IMagCalibrationStore.hpp"
#include "IRtc.hpp"
#include "MagCalibration.hpp"

#include <cstdint>

class MagCalibrationStoreRtc : public Interface::IMagCalibrationStore
{

public:

    /**
     * @brief   Bind the backup domain and the registers to use.
     * @param   firstReg: First of skPayloadRegs consecutive payload registers;
     *          the CRC32 over them is kept in the register just past them.
     * @param   frameTag: Must change whenever the meaning of the stored
     *          numbers does; a mismatched tag fails to load.
     */
    MagCalibrationStoreRtc(Interface::IRtc& rtc, uint8_t firstReg,
                           uint32_t frameTag);

    /**
     * @brief   Write a calibration; residual and sample count are not kept.
     * @retval  'true' if it read back as written; a write-protected backup
     *          domain drops writes silently.
     */
    bool save(const MagCalibration::Record& record) override;

    /**
     * @brief   Read the stored calibration. An empty domain fails the CRC too.
     * @retval  'true' if a valid record was read; record untouched otherwise.
     */
    bool load(MagCalibration::Record& record) const override;

    /// Erase the stored calibration; 'true' if every register read back erased.
    bool clear() override;

    /// Payload registers, not counting the CRC: offset 3, matrix 6, radius 1.
    static constexpr uint8_t skPayloadRegs = 10;

private:

    /// Payload first and the CRC last, so a write interrupted part-way leaves
    /// a block load() rejects; then read back, since a write-protected domain
    /// drops writes silently.
    bool writeBlock(const uint32_t payload[skPayloadRegs], uint32_t crc);

    static uint32_t toWord(float value);
    static float    toFloat(uint32_t word);

    Interface::IRtc& mRtc;
    const uint32_t   mFrameTag;
    const uint8_t    mFirstReg;
    const uint8_t    mCrcReg;

};

#endif // COMPONENT_MAG_CALIBRATION_STORE_RTC_HPP
