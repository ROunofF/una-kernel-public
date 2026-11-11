/**
 ******************************************************************************
 * @file    ProdTestVibro.cpp
 * @brief   Production test for the Vibro.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.Vibro"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"


#include "ProdTest/ProdTestVibro.hpp"
#include "ProdTest/Core/ProdTestTools.hpp"
#include "Instances/InstanceI2C.hpp"
#include "CLI/CLIConsole.h"
#include "Vibro.hpp"
#include "Gpio.hpp"
#include "SwTimer.hpp"
#include "OS.hpp"
#include "Filters/SimpleHPF.hpp"
#include "Filters/SimpleLPF.hpp"

namespace ProdTest
{

Vibro::Vibro(const std::string& id)
    : Interface::IProdTest(id)
    , mMelody{
              { Interface::IVibro::ALERT_1000MS_100, 0, 0 },
              { Interface::IVibro::ALERT_1000MS_100, 0, 0 },
              { Interface::IVibro::ALERT_1000MS_100, 0, 0 },
              { Interface::IVibro::ALERT_1000MS_100, 0, 0 },
              { Interface::IVibro::ALERT_1000MS_100, 0, 0 },
              { Interface::IVibro::ALERT_1000MS_100, 0, 0 },
              { Interface::IVibro::ALERT_1000MS_100, 0, 0 },
              { Interface::IVibro::ALERT_1000MS_100, 0, 0 },
              { Interface::IVibro::ALERT_1000MS_100, 0, 0 },
              { Interface::IVibro::ALERT_1000MS_100, 0, 0 }
          }
{}

bool Vibro::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    (void) p;

    LOG_INFO_WP("\n");
    LOG_INFO("Test Vibro (DRV2625)...\n");

    Interface::II2c &i2c = Instance::I2C::getInstance().getMainBus();

    Interface::IGpo& rst = Driver::Gpio::GetInstance(Driver::Gpio::Pin::HAPTIC_RST_N);
    rst.init(Interface::IGpo::Type::PP, true);

    //// Check address on the I2C bus
    if (!ProdTest::Tools::checkAddress(i2c, Hardware::DRV2625::skI2cAddr)) {
        LOG_ERROR("%s\n", slog.print("Vibro (DRV2625) I2C address 0x%02X not detected", Hardware::DRV2625::skI2cAddr));
        return false;
    }

    //// Test vibro
    ::Vibro &vibro = ::Vibro::GetInstance();

    vibro.config(i2c, rst);

    if (!vibro.init()) {
        LOG_ERROR("%s\n", slog.print("Failed to initialize DRV2625"));
        return false;
    }

    if (!vibro.enable()) {
        LOG_ERROR("%s\n", slog.print("Failed to enable DRV2625"));
        return false;
    }

    vibro.play(mMelody, ARRAY_SIZE(mMelody), 0);

    CLIConsole_print(CLI_UTIL_PROMPT);

    while (true) {
        OS::Delay(10);

        if (CLIInput_refresh(input) == true) {
            if (strcmp(CLIInput_getArgV(input)[0], "exit") == 0) {
                break;
            }

            checkCommand(slog, input);

            CLIInput_reset(input);
            CLIConsole_print(CLI_UTIL_PROMPT);
        }
    }

    vibro.disable();

    return true;
}

/**
 * @brief Starts the manual vibro test.
 *
 * Stops any active vibro pattern, waits shortly, and starts the test melody.
 *
 * @param slog Logger used by the production test.
 * @param argc Number of CLI arguments.
 * @param argv CLI argument list.
 */
void Vibro::startVibro(StringLogger& slog, int argc, const char** argv)
{
    (void) slog;
    (void) argc;
    (void) argv;

    ::Vibro &vibro = ::Vibro::GetInstance();

    vibro.stop();
    OS::Delay(10);

    vibro.play(mMelody, ARRAY_SIZE(mMelody), 0);
}

/**
 * @brief Verifies the vibro motor using accelerometer data.
 *
 * First measures the background vibration level with the vibro stopped.
 * Then starts the vibro motor and measures the filtered acceleration magnitude.
 * The test passes only if the vibration magnitude exceeds the threshold and
 * stays above it for at least the configured duration.
 *
 * CLI options:
 * - `-t <threshold>` Magnitude threshold in m/s^2.
 * - `-d <duration>` Minimum active vibration duration in ms.
 *
 * @param slog Logger used by the production test.
 * @param argc Number of CLI arguments.
 * @param argv CLI argument list.
 */
void Vibro::startVerify(StringLogger& slog, int argc, const char** argv)
{
    CLIParser_t p;
    CLIParser_init(&p, argc, argv);

    float threshold = 1.5f; // m/s^2
    if (CLIParser_check(&p, "-t")) {
        if (!CLIParser_getValueAsFloat(&p, "-t", &threshold) || threshold <= 0) {
            CLIConsole_print(CLI_INVALID_PARAM_MSG);
            return;
        }
    }

    const float kPeriod = 1200;  // ms

    uint32_t minDuration = kPeriod / 2;
    if (CLIParser_check(&p, "-d")) {
        if (!CLIParser_getValueAsUI32(&p, "-d", &minDuration)) {
            CLIConsole_print(CLI_INVALID_PARAM_MSG);
            return;
        }
    }

    bool vibroEnabled = true;
    if (CLIParser_check(&p, "--no-vibro")) {
        vibroEnabled = false;
    }

    bool showDetails = false;
    if (CLIParser_check(&p, "--details")) {
        showDetails = true;
    }

    bool showXLSamples = false;
    if (CLIParser_check(&p, "--xlsamples")) {
        showXLSamples = true;
    }

    const float kMagnitudeThreshold = threshold;

    Hardware::BMI270 imu(Instance::I2C::getInstance().getSensor2Bus());

    stopVibro();

    float    maxMagnitude;
    uint32_t duration;

    if (vibroEnabled) {
        if (!measuring(slog,
                       imu,
                       kMagnitudeThreshold,
                       500,
                       maxMagnitude,
                       duration,
                       showXLSamples)) {
            CLIConsole_print("FAIL\n");
            return;
        }

        LOG_INFO("threshold=%.1f, magnitude=%.2f, duration=%u\n", kMagnitudeThreshold, maxMagnitude, duration);
        if (showDetails) {
            CLIConsole_print("threshold=%.1f, magnitude=%.2f, duration=%u\n", kMagnitudeThreshold, maxMagnitude, duration);
        }

        if (maxMagnitude > kMagnitudeThreshold) {
            CLIConsole_print("FAIL\n");
            return;
        }

        startVibro();
    }

    if (!measuring(slog,
                   imu,
                   kMagnitudeThreshold,
                   kPeriod,
                   maxMagnitude,
                   duration,
                   showXLSamples)) {
        CLIConsole_print("FAIL\n");
        return;
    }

    LOG_INFO("threshold=%.1f, magnitude=%.2f, duration=%u\n", kMagnitudeThreshold, maxMagnitude, duration);
    if (showDetails) {
        CLIConsole_print("threshold=%.1f, magnitude=%.2f, duration=%u\n", kMagnitudeThreshold, maxMagnitude, duration);
    }

    if (maxMagnitude < kMagnitudeThreshold) {
        CLIConsole_print("FAIL\n");
        return;
    }

    if (duration < minDuration) {
        CLIConsole_print("FAIL\n");
        return;
    }

    CLIConsole_print("OK\n");
}

/**
 * @brief Handles internal vibro test commands.
 *
 * Supported commands:
 * - `vibro`  Starts the manual vibro test.
 * - `verify` Starts the IMU-based vibro verification.
 *
 * @param slog  Logger used by the production test.
 * @param input CLI input object containing command arguments.
 */
void Vibro::checkCommand(StringLogger& slog, CLIInput_t* input)
{
    int          argc = CLIInput_getArgC(input);
    const char** argv = CLIInput_getArgV(input);

    if (strcmp(argv[0], "vibro") == 0) {
        startVibro(slog, argc, argv);
    } else if (strcmp(argv[0], "verify") == 0) {
        startVerify(slog, argc, argv);
    } else {
        CLIConsole_print("unknown command\n");
    }
}

/**
 * @brief Initializes and configures the BMI270 accelerometer.
 *
 * Configures the accelerometer at 100 Hz with a +/-2 g range and enables it
 * for vibro verification measurements.
 *
 * @param slog Logger used by the production test.
 * @param imu  BMI270 instance to initialize.
 *
 * @return true if the accelerometer was initialized and enabled successfully.
 * @return false otherwise.
 */
bool Vibro::startImu(StringLogger& slog, Hardware::BMI270& imu)
{
    if (!imu.isReady()) {
        LOG_ERROR("%s\n", slog.print("I2C address not detected"));
        return false;
    }

    // Requesting a sensor also brings the chip up
    Hardware::BMI270::SensorConfig accelConfig {};
    accelConfig.periodUs        = 10000;    // 100 Hz
    accelConfig.range           = 2;        // +/-2 g
    accelConfig.performanceMode = false;
    // One sample per reading. Averaging, the driver's default, halves the
    // motor's amplitude, and the threshold was tuned without it.
    accelConfig.bandwidth       = Hardware::BMI270::Bandwidth::OSR4_AVG1;

    if (!imu.requestSensor(Hardware::BMI270::SensorType::ACCELEROMETER, accelConfig)) {
        LOG_ERROR("%s\n", slog.print("Failed to enable accelerometer"));
        stopImu(imu);
        return false;
    }

    return true;
}

/**
 * @brief Stops and deinitializes the BMI270 accelerometer.
 *
 * @param imu BMI270 instance to stop.
 */
void Vibro::stopImu(Hardware::BMI270& imu)
{
    imu.releaseAll();
}

/**
 * @brief Measures vibration level using filtered accelerometer data.
 *
 * Reads accelerometer samples during the specified period, applies a high-pass
 * filter to each axis, calculates the Euclidean norm of the filtered vector,
 * smooths it with a low-pass filter, and measures how long the resulting
 * magnitude stays above the specified threshold.
 *
 * @param slog               Logger used by the production test.
 * @param imu                BMI270 instance used for measurements.
 * @param magnitudeThreshold Threshold used to detect active vibration.
 * @param period             Measurement period in ms.
 * @param maxMagnitude       Output maximum filtered magnitude detected during measurement.
 * @param duration           Output total time in ms when magnitude was above the threshold.
 *
 * @return true if measurement completed successfully.
 * @return false if IMU initialization or accelerometer reading failed.
 */
bool Vibro::measuring(StringLogger&     slog,
                      Hardware::BMI270& imu,
                      float             magnitudeThreshold,
                      uint32_t          period,
                      float&            maxMagnitude,
                      uint32_t&         duration,
                      bool              showXLSamples)
{
    if (!startImu(slog, imu)) {
        return false;
    }

    OS::Delay(10); // Give time for initialization

    Filter::SimpleHPF hpfX(0.98);
    Filter::SimpleHPF hpfY(0.98);
    Filter::SimpleHPF hpfZ(0.98);

    Filter::SimpleLPF lpf(0.9);

    maxMagnitude = 0;
    duration     = 0;

    Driver::SwTimer durationTimer;
    Driver::SwTimer t(period);
    while (!t.expired()) {
        OS::Delay(15);

        // Try to read accelerometer data
        Hardware::BMI270::SensorData accelData {};
        if (!imu.readAccelerometer(accelData)) {
            LOG_ERROR("%s\n", slog.print("Failed to read data"));
            stopImu(imu);
            return false;
        }

        LOG_DEBUG("%s\n", slog.print("Data: X=%.3f Y=%.3f Z=%.3f (m/s^2)", accelData.x, accelData.y, accelData.z));
        if (showXLSamples) {
            CLIConsole_print("Data: X=%.3f Y=%.3f Z=%.3f\n", accelData.x, accelData.y, accelData.z);
        }

        hpfX.execute(accelData.x);
        hpfY.execute(accelData.y);
        hpfZ.execute(accelData.z);

        const float squaredMagnitude = hpfX.getValue() * hpfX.getValue() +
                                       hpfY.getValue() * hpfY.getValue() +
                                       hpfZ.getValue() * hpfZ.getValue();

        const float magnitude = lpf.execute(sqrt(squaredMagnitude));

        if (maxMagnitude < magnitude) {
            maxMagnitude = magnitude;
        }

        if (magnitude >= magnitudeThreshold && !durationTimer.isActive()) {
            durationTimer.start();
        } else if (magnitude < magnitudeThreshold && durationTimer.isActive()) {
            duration += durationTimer.passed();
            durationTimer.stop();
        }
    }

    stopImu(imu);

    if (durationTimer.isActive()) {
        duration += durationTimer.passed();
    }

    return true;
}

/**
 * @brief Starts a short vibro pulse used for automatic verification.
 *
 * Stops any active vibro pattern, waits shortly, and starts a 1-second
 * vibration effect.
 */
void Vibro::startVibro()
{
    ::Vibro &vibro = ::Vibro::GetInstance();

    vibro.stop();
    OS::Delay(10);

    vibro.play(Interface::IVibro::ALERT_1000MS_100);
}

/**
 * @brief Stops the vibro motor.
 *
 * Stops any active vibro pattern and waits shortly to let the motor settle.
 */
void Vibro::stopVibro()
{
    ::Vibro &vibro = ::Vibro::GetInstance();

    vibro.stop();
    OS::Delay(100);
}

}   // namespace ProdTest
