/**
 ******************************************************************************
 * @file    ProdTestImu.cpp
 * @brief   Production test for the IMU.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.IMU"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"


#include "ProdTest/ProdTestIMU.hpp"
#include "Instances/InstanceI2C.hpp"
#include "Gpio.hpp"
#include "N24S64B.hpp"
#include "OS.hpp"

namespace ProdTest
{

namespace
{

/// Brings the chip up on first use: bmi270_init(), the watch-axis remap, then
/// the accelerometer at 100 Hz, +/-2 g.
bool requestAccel(Hardware::BMI270& imu)
{
    Hardware::BMI270::SensorConfig accelConfig {};
    accelConfig.periodUs        = 10000;
    accelConfig.range           = 2;
    accelConfig.performanceMode = false;

    return imu.requestSensor(Hardware::BMI270::SensorType::ACCELEROMETER, accelConfig);
}

}   // namespace

Imu::Imu(const std::string& id) : Interface::IProdTest(id)
{}

bool Imu::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test IMU (BMI270)...\n");

    //////////////////////////////////////
    //// Check address on the I2C bus
    //////////////////////////////////////

    Hardware::BMI270 imu(Instance::I2C::getInstance().getSensor2Bus());

    if (!imu.isReady()) {
        LOG_ERROR("%s\n", slog.print("I2C address not detected"));
        return false;
    }

    if (!requestAccel(imu)) {
        LOG_ERROR("%s\n", slog.print("Failed to init"));
        imu.releaseAll();
        return false;
    }

    bool ok = run(slog, imu);

    imu.releaseAll();

    return ok;
}

bool Imu::run(StringLogger& slog, Hardware::BMI270& imu)
{
    OS::Delay(TEST_DELAY);

    // Pin test
    LOG_INFO("Test INT2 pin ...\n");
    if (testLoopback(imu)) {
        LOG_INFO("%s\n", slog.print("INT2 pin OK"));
    } else {
        LOG_ERROR("%s\n", slog.print("INT2 pin failed"));
        return false;
    }

    OS::Delay(TEST_DELAY);

    // Self-test
    LOG_INFO("Self-test...\n");
    if (imu.selfTest()) {
        LOG_INFO("%s\n", slog.print("Self-test: OK"));
    } else {
        LOG_ERROR("%s\n", slog.print("Self-test: failed"));
        return false;
    }

    OS::Delay(TEST_DELAY);

    LOG_INFO("Read data...\n");
    // The self-test ends in a soft reset, so the accelerometer has to be
    // requested again before it gives data.
    if (!requestAccel(imu)) {
        LOG_ERROR("%s\n", slog.print("Failed to enable accelerometer"));
        return false;
    }

    OS::Delay(100); // Give time for initialization

    // Watch axes: +X out of 3 o'clock, +Y out of 12 o'clock, +Z out of the face
    Hardware::BMI270::SensorData accelData {};
    if (imu.readAccelerometer(accelData)) {
        LOG_INFO("%s\n", slog.print("Data: X=%.3f Y=%.3f Z=%.3f (m/s^2)", accelData.x, accelData.y, accelData.z));
    } else {
        LOG_ERROR("%s\n", slog.print("Failed to read data"));
        return false;
    }

    return true;
}

bool Imu::testLoopback(Hardware::BMI270 &imu)
{
    // Initialize pin as input with pull-up for testing
    Driver::Gpio &int2 = Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSOR2_INT2);
    int2.init(Interface::IGpi::Pull::UP);

    // Drive INT2: active high, push-pull. With no interrupt mapped to it the
    // chip holds it at its inactive level, low.
    Hardware::BMI270::InterruptConfig driven {};
    driven.outputEnabled = true;
    driven.activeHigh    = true;
    driven.pushPull      = true;

    if (!imu.configureInterruptPin(Hardware::BMI270::InterruptPin::INT2, driven)) {
        LOG_ERROR("failed to enable interrupt line INT2\n");
        int2.deinit();
        return false;
    }

    OS::Delay(10);
    const bool state1 = int2.get();     // LOW

    // Release INT2: the pull-up takes the line high.
    Hardware::BMI270::InterruptConfig released {};
    released.outputEnabled = false;
    released.activeHigh    = true;
    released.pushPull      = true;

    if (!imu.configureInterruptPin(Hardware::BMI270::InterruptPin::INT2, released)) {
        LOG_ERROR("failed to disable interrupt line INT2\n");
        int2.deinit();
        return false;
    }

    OS::Delay(10);
    const bool state2 = int2.get();    // HIGH

    int2.deinit();

    LOG_INFO("state1 = %d\n", (int)state1);
    LOG_INFO("state2 = %d\n", (int)state2);

    return !state1 && state2;
}


}   // namespace ProdTest
