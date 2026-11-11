/**
 ******************************************************************************
 * @file    InstanceMagCal.cpp
 * @brief   The watch's magnetometer calibration.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Inst.MagCal"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Instances/InstanceMagCal.hpp"

#include "Magnetometer/MagCorrection.hpp"
#include "Magnetometer/MagCalibrationStoreRtc.hpp"
#include "Rtc.hpp"

namespace Instance {

namespace {

// The store assumes a consecutive payload followed by the CRC, while the
// register map names each element separately. These keep the two in step.
static_assert(RTC_BRK_REG_MAG_CAL_OFFSET_Y == RTC_BRK_REG_MAG_CAL_OFFSET_X + 1, "");
static_assert(RTC_BRK_REG_MAG_CAL_OFFSET_Z == RTC_BRK_REG_MAG_CAL_OFFSET_X + 2, "");
static_assert(RTC_BRK_REG_MAG_CAL_M00      == RTC_BRK_REG_MAG_CAL_OFFSET_X + 3, "");
static_assert(RTC_BRK_REG_MAG_CAL_M01      == RTC_BRK_REG_MAG_CAL_OFFSET_X + 4, "");
static_assert(RTC_BRK_REG_MAG_CAL_M02      == RTC_BRK_REG_MAG_CAL_OFFSET_X + 5, "");
static_assert(RTC_BRK_REG_MAG_CAL_M11      == RTC_BRK_REG_MAG_CAL_OFFSET_X + 6, "");
static_assert(RTC_BRK_REG_MAG_CAL_M12      == RTC_BRK_REG_MAG_CAL_OFFSET_X + 7, "");
static_assert(RTC_BRK_REG_MAG_CAL_M22      == RTC_BRK_REG_MAG_CAL_OFFSET_X + 8, "");
static_assert(RTC_BRK_REG_MAG_CAL_RADIUS   == RTC_BRK_REG_MAG_CAL_OFFSET_X + 9, "");
static_assert(MagCalibrationStoreRtc::skPayloadRegs == 10,
              "the register map names ten payload elements");
static_assert(RTC_BRK_REG_MAG_CAL_CRC ==
              RTC_BRK_REG_MAG_CAL_OFFSET_X + MagCalibrationStoreRtc::skPayloadRegs,
              "the CRC register must sit just past the payload");

// The axis frame stored corrections are expressed in: how the part is mounted
// on this board. Change the value whenever the mounting changes; old records
// then stop loading and the watch asks to be calibrated again.
constexpr uint32_t kAxisFrameTag = 0x4D414732U;

/// The correction, built and loaded on first use. Construction takes a lock
/// and reads the store, so first use must come after the scheduler starts.
MagCorrection& correctionInstance()
{
    static MagCalibrationStoreRtc sStore(Driver::Rtc::GetInstance(),
                                 RTC_BRK_REG_MAG_CAL_OFFSET_X,
                                 kAxisFrameTag);

    static MagCorrection sCorrection(sStore);

    return sCorrection;
}

} // namespace

Interface::IMagCorrection& MagCal::correction()
{
    return correctionInstance();
}

bool MagCal::store(const MagCalibration::Record& record)
{
    return correctionInstance().setCalibration(record);
}

bool MagCal::read(MagCalibration::Record& record)
{
    return correctionInstance().getCalibration(record);
}

bool MagCal::clear()
{
    return correctionInstance().clearCalibration();
}

} // namespace Instance
