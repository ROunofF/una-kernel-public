/**
******************************************************************************
 * @file    PAH8316LSFactoryTester.cpp
 * @brief   Factory tester for the PAH8316LS
 *
 ******************************************************************************
 */

#include "PAH8316LSFactoryTester.hpp"
#include "System.hpp"
#include "OS.hpp"

#define LOG_MODULE_PRX      "FTest"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace Hardware
{

/**
 * @brief Constructor with I2C configuration.
 * @param i2c: Reference to the I2C object.
 * @param devAddr: Target device address. 7 bit, not shifted.
 */
PAH8316LSFactoryTester::PAH8316LSFactoryTester(Interface::II2c& i2c,
                                               Interface::IGpi& pinInt0,
                                               Interface::IGpi& pinInt1,
                                               Interface::IGpo& sensEn,
                                               Interface::IGpo& ledEn,
                                               uint8_t          devAddr)
    : mI2C(i2c)
    , mPinInt0(pinInt0)
    , mPinInt1(pinInt1)
    , mSensEn(sensEn)
    , mLedEn(ledEn)
    , mAddr(devAddr)
    , mIsEnabled(false)
    , mPlat {
        .plat = {
            .user_data  = this,
            .type       = PAH_PLAT_COMM_I2C,
            .max_length = 250,
            .write      = plat_write,
            .read       = plat_read,
            .delay_ms   = plat_delay_ms,
            .ticks      = nullptr,
        },
        .read_int = plat_read_int,
    }
{
}

PAH8316LSFactoryTester::~PAH8316LSFactoryTester()
{
    deinit();
}

bool PAH8316LSFactoryTester::checkID()
{
    init();
    enable(true);

    uint16_t ID = 0;
    pah831x_factory_test_read_id(&mPlat, &ID);

    deinit();

    return (ID == 0x8311);
}

bool PAH8316LSFactoryTester::demo_factory_mode(pah831x_factory_test_e test_mode)
{
    init();
    enable(true);

    if (test_mode == INT_TEST) {
        mPinInt0.init(Interface::IGpi::Pull::UP);
        mPinInt1.init(Interface::IGpi::Pull::UP);
    }

    bool verify_result = pah831x_factory_test_run(&mPlat, test_mode);

    if (test_mode == INT_TEST) {
        mPinInt0.deinit();
        mPinInt1.deinit();
    }

    deinit();

    if (verify_result) {
        LOG_INFO("TEST Pass\n\n");
    } else {
        LOG_INFO("TEST Fail\n\n");
    }

    return verify_result;
}

void PAH8316LSFactoryTester::init()
{
    mSensEn.init(Interface::IGpo::Type::PP, false, false);
    mLedEn.init(Interface::IGpo::Type::PP, false, false);
}

void PAH8316LSFactoryTester::deinit()
{
    if (mIsEnabled) {
        enable(false);
    }
}

void PAH8316LSFactoryTester::enable(bool en)
{
    if (en) {
        mSensEn.set(true);
        mLedEn.set(true);
    } else {
        mLedEn.set(false);
        mSensEn.set(false);
    }

    mIsEnabled = en;
}

bool PAH8316LSFactoryTester::writeMem(uint8_t addr, const uint8_t *data, size_t len)
{
    if (!data) {
        return false;
    }

    bool success = false;
    for (uint32_t retry = 0; retry <= skI2cRetries; retry++) {
        if (mI2C.writeMem(mAddr, static_cast<uint16_t>(addr), 1, data, static_cast<uint16_t>(len))) {
            success = true;
            break;
        }

        if (retry < skI2cRetries) {
            LOG_WARNING("I2C write retry %d\n", retry + 1);
            OS::Delay(1);
        }
    }

    if (!success) {
        LOG_ERROR("Failed to write register 0x%02X after %d retries\n", addr, skI2cRetries);
    }

    return success;
}

bool PAH8316LSFactoryTester::readMem(uint8_t addr, uint8_t *data, size_t len)
{
    if (!data) {
         return false;
    }

    bool success = false;
    for (uint32_t retry = 0; retry <= skI2cRetries; retry++) {
        if (mI2C.readMem(mAddr, static_cast<uint16_t>(addr), 1, data, static_cast<uint16_t>(len))) {
            success = true;
            break;
        }

        if (retry < skI2cRetries) {
            LOG_WARNING("I2C read retry %d\n", retry + 1);
            OS::Delay(1);
        }
    }

    if (!success) {
        LOG_ERROR("Failed to read register 0x%02X after %d retries\n", addr, skI2cRetries);
    }

    return success;
}

pah_ret PAH8316LSFactoryTester::plat_write(uint8_t addr, const uint8_t *data, size_t write_size, void *pArgs)
{
    auto *self = static_cast<PAH8316LSFactoryTester *>(pArgs);
    return self->writeMem(addr, data, write_size) ? PAH_RET_SUCCESS : PAH_RET_PLAT_FAILED;
}

pah_ret PAH8316LSFactoryTester::plat_read(uint8_t addr, uint8_t *data, size_t read_size, void *pArgs)
{
    auto *self = static_cast<PAH8316LSFactoryTester *>(pArgs);
    return self->readMem(addr, data, read_size) ? PAH_RET_SUCCESS : PAH_RET_PLAT_FAILED;
}

void PAH8316LSFactoryTester::plat_delay_ms(uint32_t ms, void *pArgs)
{
    (void)pArgs;
    Driver::System::Delay(ms);
}

bool PAH8316LSFactoryTester::plat_read_int(uint8_t pin, void *pArgs)
{
    auto *self = static_cast<PAH8316LSFactoryTester *>(pArgs);
    return (pin == 0) ? self->mPinInt0.get() : self->mPinInt1.get();
}

} // Hardware
