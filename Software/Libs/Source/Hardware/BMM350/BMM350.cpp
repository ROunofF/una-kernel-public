/**
 ******************************************************************************
 * @file    BMM350.cpp
 * @brief   BMM350 3-axis magnetometer driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Hardware/BMM350/BMM350.hpp"

#define LOG_MODULE_PRX      "BMM350"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "bmm350.h"
// The SensorAPI's delays run from tens of microseconds to tens of
// milliseconds: UsTimer covers the sub-tick end, OS::Delay the rest.
#include "UsTimer.hpp"
#include "OS.hpp"

namespace Hardware
{

// clampAveraging() compares enumerators, which is only meaningful while the
// vendor's numbering ascends.
static_assert(BMM350::Averaging::AVG_1 < BMM350::Averaging::AVG_2, "averaging must ascend");
static_assert(BMM350::Averaging::AVG_2 < BMM350::Averaging::AVG_4, "averaging must ascend");
static_assert(BMM350::Averaging::AVG_4 < BMM350::Averaging::AVG_8, "averaging must ascend");

// isReady() and bmm350_init() must agree on what a BMM350 is.
static_assert(BMM350::skChipId == BMM350_CHIP_ID,
              "the chip id here must match the one the SensorAPI checks");

BMM350::BMM350()
{

}

BMM350::BMM350(Interface::II2c& i2c, uint8_t devAddr)
{
    configure(i2c, devAddr);
}

void BMM350::configure(Interface::II2c& i2c, uint8_t devAddr)
{
    mpI2c = &i2c;
    mAddr = devAddr;

    Driver::UsTimer::Init();

    mDev = {};
    mDev.intf_ptr = this;
    mDev.read     = i2cRead;
    mDev.write    = i2cWrite;
    mDev.delay_us = delayUs;

    mIsConfigured = true;
    mIsInited     = false;
}

bool BMM350::isReady()
{
    if (!mIsConfigured) {
        return false;
    }

    if (!mpI2c->isDeviceReady(mAddr)) {
        return false;
    }

    uint8_t chipId = 0;
    if (!readChipId(chipId)) {
        return false;
    }

    return chipId == skChipId;
}

bool BMM350::readChipId(uint8_t& chipId)
{
    if (!mIsConfigured) {
        return false;
    }

    // Through the SensorAPI, which strips the two leading dummy bytes every
    // BMM350 read carries.
    return bmm350_get_regs(BMM350_REG_CHIP_ID, &chipId, 1, &mDev) == BMM350_OK;
}

bool BMM350::init()
{
    if (!mIsConfigured) {
        LOG_ERROR("Not configured\n");
        return false;
    }

    mIsInited = false;

    // Soft reset, CHIP_ID check, OTP download, magnetic reset. A part whose
    // CHIP_ID does not match is rejected with BMM350_E_DEV_NOT_FOUND.
    int8_t rslt = bmm350_init(&mDev);
    if (rslt != BMM350_OK) {
        LOG_ERROR("bmm350_init failed, rslt=%d\n", rslt);
        return false;
    }

    rslt = bmm350_enable_axes(BMM350_X_EN, BMM350_Y_EN, BMM350_Z_EN, &mDev);
    if (rslt != BMM350_OK) {
        LOG_ERROR("bmm350_enable_axes failed, rslt=%d\n", rslt);
        return false;
    }

    mIsInited = true;

    // No rate is programmed here; the reset default stands.
    LOG_INFO("Initialized, var_id=0x%02X\n", mDev.var_id);

    return true;
}

bool BMM350::isInitialized() const
{
    return mIsInited;
}

bool BMM350::reset()
{
    if (!mIsConfigured) {
        return false;
    }

    mIsInited = false;

    int8_t rslt = bmm350_soft_reset(&mDev);
    if (rslt != BMM350_OK) {
        LOG_ERROR("bmm350_soft_reset failed, rslt=%d\n", rslt);
        return false;
    }

    return init();
}

bool BMM350::setOdr(Odr odr, Averaging avg)
{
    if (!mIsInited) {
        return false;
    }

    const Averaging applied = clampAveraging(odr, avg);

    int8_t rslt = bmm350_set_odr_performance(
            static_cast<bmm350_data_rates>(odr),
            static_cast<bmm350_performance_parameters>(applied),
            &mDev);
    if (rslt != BMM350_OK) {
        LOG_ERROR("bmm350_set_odr_performance failed, rslt=%d\n", rslt);
        return false;
    }

    if (applied != avg) {
        LOG_WARNING("Averaging reduced %u -> %u for odr=%u\n",
                static_cast<unsigned>(avg), static_cast<unsigned>(applied),
                static_cast<unsigned>(odr));
    }

    return true;
}

bool BMM350::setPowerMode(PowerMode mode)
{
    if (!mIsInited) {
        return false;
    }

    int8_t rslt = bmm350_set_powermode(
            static_cast<bmm350_power_modes>(mode), &mDev);
    if (rslt != BMM350_OK) {
        LOG_ERROR("bmm350_set_powermode(%u) failed, rslt=%d\n",
                static_cast<unsigned>(mode), rslt);
        return false;
    }

    return true;
}

bool BMM350::configureInterrupt(const InterruptConfig& config)
{
    if (!mIsInited) {
        return false;
    }

    int8_t rslt = bmm350_configure_interrupt(
            config.latched    ? BMM350_LATCHED     : BMM350_PULSED,
            config.activeHigh ? BMM350_ACTIVE_HIGH : BMM350_ACTIVE_LOW,
            config.pushPull   ? BMM350_INTR_PUSH_PULL : BMM350_INTR_OPEN_DRAIN,
            BMM350_MAP_TO_PIN,
            &mDev);
    if (rslt != BMM350_OK) {
        LOG_ERROR("bmm350_configure_interrupt failed, rslt=%d\n", rslt);
        return false;
    }

    return true;
}

bool BMM350::enableInterrupt(bool enable)
{
    if (!mIsInited) {
        return false;
    }

    int8_t rslt = bmm350_enable_interrupt(
            enable ? BMM350_ENABLE_INTERRUPT : BMM350_DISABLE_INTERRUPT,
            &mDev);
    if (rslt != BMM350_OK) {
        LOG_ERROR("bmm350_enable_interrupt(%u) failed, rslt=%d\n",
                enable ? 1U : 0U, rslt);
        return false;
    }

    return true;
}

bool BMM350::getDataReady(bool& ready)
{
    if (!mIsInited) {
        return false;
    }

    uint8_t status = 0;
    int8_t  rslt   = bmm350_get_interrupt_status(&status, &mDev);
    if (rslt != BMM350_OK) {
        return false;
    }

    ready = (status != 0);

    return true;
}

bool BMM350::readSample(Sample& sample)
{
    if (!mIsInited) {
        return false;
    }

    bmm350_mag_temp_data data = {};

    int8_t rslt = bmm350_get_compensated_mag_xyz_temp_data(&data, &mDev);
    if (rslt != BMM350_OK) {
        return false;
    }

    sample.x           = data.x;
    sample.y           = data.y;
    sample.z           = data.z;
    sample.temperature = data.temperature;
    sample.sensorTime  = data.sensortime;

    return true;
}

bool BMM350::measureForced(Sample& sample)
{
    if (!mIsInited) {
        return false;
    }

    // set_powermode() waits out the conversion, so the data registers hold
    // the new sample when it returns.
    if (!setPowerMode(PowerMode::FORCED)) {
        return false;
    }

    return readSample(sample);
}

bool BMM350::setI2cWatchdog(bool enable, bool longTimeout)
{
    if (!mIsInited) {
        return false;
    }

    int8_t rslt = bmm350_set_i2c_wdt(
            enable      ? BMM350_I2C_WDT_EN       : BMM350_I2C_WDT_DIS,
            longTimeout ? BMM350_I2C_WDT_SEL_LONG : BMM350_I2C_WDT_SEL_SHORT,
            &mDev);
    if (rslt != BMM350_OK) {
        LOG_ERROR("bmm350_set_i2c_wdt failed, rslt=%d\n", rslt);
        return false;
    }

    return true;
}

BMM350::Odr BMM350::periodUsToOdr(uint32_t periodUs)
{
    // Fastest to slowest, keeping the last rung that still fits; a request
    // faster than the top rung saturates on the first. Periods come from
    // odrToPeriodUs() so the two directions cannot disagree.
    static constexpr Odr kLadder[] = {
        Odr::ODR_400_HZ,
        Odr::ODR_200_HZ,
        Odr::ODR_100_HZ,
        Odr::ODR_50_HZ,
        Odr::ODR_25_HZ,
        Odr::ODR_12_5_HZ,
        Odr::ODR_6_25_HZ,
        Odr::ODR_3_125_HZ,
        Odr::ODR_1_5625_HZ,
    };

    Odr result = kLadder[0];
    for (Odr odr : kLadder) {
        if (odrToPeriodUs(odr) > periodUs) {
            break;
        }
        result = odr;
    }

    return result;
}

uint32_t BMM350::odrToPeriodUs(Odr odr)
{
    switch (odr) {
        case Odr::ODR_400_HZ:    return 2500;
        case Odr::ODR_200_HZ:    return 5000;
        case Odr::ODR_100_HZ:    return 10000;
        case Odr::ODR_50_HZ:     return 20000;
        case Odr::ODR_25_HZ:     return 40000;
        case Odr::ODR_12_5_HZ:   return 80000;
        case Odr::ODR_6_25_HZ:   return 160000;
        case Odr::ODR_3_125_HZ:  return 320000;
        case Odr::ODR_1_5625_HZ: return 640000;
    }

    // No default label, so a new rung must state its period. Reachable only
    // through a bad cast; zero is obviously not a period.
    return 0;
}

BMM350::Averaging BMM350::clampAveraging(Odr odr, Averaging avg)
{
    // Datasheet table 5. The SensorAPI applies the same reduction silently;
    // doing it here is what lets the caller be told.
    switch (odr) {
        case Odr::ODR_400_HZ:
            return Averaging::AVG_1;

        case Odr::ODR_200_HZ:
            return (avg > Averaging::AVG_2) ? Averaging::AVG_2 : avg;

        case Odr::ODR_100_HZ:
            return (avg > Averaging::AVG_4) ? Averaging::AVG_4 : avg;

        // Slow enough that even AVG_8 folds inside one tick.
        case Odr::ODR_50_HZ:
        case Odr::ODR_25_HZ:
        case Odr::ODR_12_5_HZ:
        case Odr::ODR_6_25_HZ:
        case Odr::ODR_3_125_HZ:
        case Odr::ODR_1_5625_HZ:
            break;
    }

    // No default label, so a new rung must state its own ceiling.
    return avg;
}

bool BMM350::readRegs(uint8_t reg, uint8_t* pData, uint32_t len)
{
    if (mpI2c == nullptr || pData == nullptr) {
        return false;
    }

    for (uint32_t retry = 0; retry <= skI2cRetries; retry++) {
        if (mpI2c->readMem(mAddr, static_cast<uint16_t>(reg), 1, pData,
                static_cast<uint16_t>(len))) {
            return true;
        }

        if (retry < skI2cRetries) {
            delayUs(skRetryDelayUs, nullptr);
        }
    }

    return false;
}

bool BMM350::writeRegs(uint8_t reg, const uint8_t* pData, uint32_t len)
{
    if (mpI2c == nullptr || pData == nullptr) {
        return false;
    }

    for (uint32_t retry = 0; retry <= skI2cRetries; retry++) {
        if (mpI2c->writeMem(mAddr, static_cast<uint16_t>(reg), 1, pData,
                static_cast<uint16_t>(len))) {
            return true;
        }

        if (retry < skI2cRetries) {
            delayUs(skRetryDelayUs, nullptr);
        }
    }

    return false;
}

BMM350_INTF_RET_TYPE BMM350::i2cRead(uint8_t reg, uint8_t* pData, uint32_t len,
        void* pIntf)
{
    BMM350* pSelf = static_cast<BMM350*>(pIntf);
    if (pSelf == nullptr) {
        return BMM350_E_NULL_PTR;
    }

    // len already includes the two dummy bytes the caller expects to strip.
    return pSelf->readRegs(reg, pData, len) ? BMM350_INTF_RET_SUCCESS
                                            : BMM350_E_COM_FAIL;
}

BMM350_INTF_RET_TYPE BMM350::i2cWrite(uint8_t reg, const uint8_t* pData,
        uint32_t len, void* pIntf)
{
    BMM350* pSelf = static_cast<BMM350*>(pIntf);
    if (pSelf == nullptr) {
        return BMM350_E_NULL_PTR;
    }

    return pSelf->writeRegs(reg, pData, len) ? BMM350_INTF_RET_SUCCESS
                                             : BMM350_E_COM_FAIL;
}

void BMM350::delayUs(uint32_t periodUs, void* pIntf)
{
    (void) pIntf;

    // A millisecond and up goes to the scheduler rather than a busy wait.
    // Rounded up and padded by two, because a scheduled wait can only come
    // out short, and these are settling times the part mandates.
    if (periodUs < 1000) {
        Driver::UsTimer::DelayUs(periodUs);
    } else {
        OS::Delay(((periodUs + 999) / 1000) + 2);
    }
}

} // namespace Hardware
