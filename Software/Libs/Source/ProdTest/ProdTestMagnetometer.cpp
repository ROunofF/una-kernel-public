/**
 ******************************************************************************
 * @file    ProdTestMagnetometer.cpp
 * @brief   Production test for the Magnetometer.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.Magnetometer"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"


#include "ProdTest/ProdTestMagnetometer.hpp"
#include "Instances/InstanceI2C.hpp"
#include "Gpio.hpp"
#include "OS.hpp"

#include <cmath>

namespace ProdTest
{

// Each line given to the StringLogger becomes one comma-separated field of the
// result record, so a message carries no comma and stands on its own.

Magnetometer::Magnetometer(const std::string& id) : Interface::IProdTest(id)
{}

Magnetometer::~Magnetometer()
{
    releasePin();
}

bool Magnetometer::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    (void) input;

    LOG_INFO_WP("\n");
    LOG_INFO("Test Magnetometer (BMM350)...\n");

    const bool checkInt = CLIParser_check(&p, "--int");

    // No supply to switch: the part boots into suspend.
    Hardware::BMM350 magnetometer(Instance::I2C::getInstance().getSensor2Bus());

    // MAG_INT is on the line the pin map names SENSOR2_MISO; not a slip.
    mpIntPin = &Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSOR2_MISO);

    if (!runProbe(slog, magnetometer)) {
        return false;
    }

    if (checkInt && !runInterrupt(slog, magnetometer)) {
        return false;
    }

    return true;
}

bool Magnetometer::runProbe(StringLogger& slog, Hardware::BMM350& magnetometer)
{
    uint8_t chipId = 0;
    if (!magnetometer.readChipId(chipId)) {
        LOG_ERROR("%s\n", slog.print("I2C address not detected"));
        return false;
    }

    LOG_INFO("%s\n", slog.print("Chip ID: 0x%02X", chipId));

    if (chipId != Hardware::BMM350::skChipId) {
        LOG_ERROR("%s\n", slog.print("Unexpected chip ID 0x%02X (expected 0x%02X)",
                chipId, Hardware::BMM350::skChipId));
        return false;
    }

    if (!magnetometer.init()) {
        LOG_ERROR("%s\n", slog.print("Failed to init"));
        return false;
    }

    // A forced conversion needs no interrupt line and leaves the part in
    // suspend.
    Hardware::BMM350::Sample sample;
    if (!magnetometer.measureForced(sample)) {
        LOG_ERROR("%s\n", slog.print("Failed to get data"));
        return false;
    }

    const float magnitude = sqrtf(sample.x * sample.x +
                                  sample.y * sample.y +
                                  sample.z * sample.z);

    LOG_INFO("%s\n", slog.print("X: %f uT", sample.x));
    LOG_INFO("%s\n", slog.print("Y: %f uT", sample.y));
    LOG_INFO("%s\n", slog.print("Z: %f uT", sample.z));
    LOG_INFO("%s\n", slog.print("|B|: %f uT", magnitude));
    LOG_INFO("%s\n", slog.print("Temperature: %f", sample.temperature));

    // All zero is a part that answers but never converts. The magnitude is
    // not range-checked: the fixture decides the field.
    if (magnitude == 0.0f) {
        LOG_ERROR("%s\n", slog.print("Degenerate reading: all axes zero"));
        return false;
    }

    return true;
}

bool Magnetometer::runInterrupt(StringLogger& slog,
        Hardware::BMM350& magnetometer)
{
    LOG_INFO("Checking the data-ready line...\n");

    if (!startStream(magnetometer)) {
        LOG_ERROR("%s\n", slog.print("Failed to start streaming"));
        stopStream(magnetometer);
        return false;
    }

    const uint32_t start   = OS::GetTimeMs();
    uint32_t       samples = 0;

    while ((OS::GetTimeMs() - start) < skIntWindowMs) {
        Hardware::BMM350::Sample sample;
        if (waitSample(magnetometer, sample, skSampleTimeoutMs)) {
            samples++;
        }
    }

    const uint32_t elapsed = OS::GetTimeMs() - start;
    const uint32_t edges   = mEdges.load(std::memory_order_relaxed);

    stopStream(magnetometer);

    // From the time actually run: the last wait overshoots the window, and
    // the edges counted include the overshoot.
    const uint32_t expected = elapsed / skSamplePeriodMs;

    LOG_INFO("%s\n", slog.print("INT edges=%u samples=%u expected=~%u over %ums",
            static_cast<unsigned>(edges),
            static_cast<unsigned>(samples),
            static_cast<unsigned>(expected),
            static_cast<unsigned>(elapsed)));

    // Only the edge count sees a line firing too often: something is always
    // latched, so the reads keep up.
    if (edges > expected * skIntMaxEdgeFactor) {
        LOG_ERROR("%s\n", slog.print("Data-ready line fires far too often"));
        return false;
    }

    if (samples < skIntMinSamples) {
        // No edges is routing; edges without samples is the bus or this loop.
        if (edges == 0) {
            LOG_ERROR("%s\n", slog.print("Data-ready line never fired"));
        } else {
            LOG_ERROR("%s\n", slog.print(
                    "Data-ready line fires but samples are not being read"));
        }
        return false;
    }

    return true;
}

bool Magnetometer::startStream(Hardware::BMM350& magnetometer)
{
    // Set here: the expected counts are derived from skSamplePeriodMs.
    if (!magnetometer.setOdr(skOdr, skAveraging)) {
        return false;
    }

    Hardware::BMM350::InterruptConfig intConfig;
    intConfig.latched    = true;
    intConfig.activeHigh = true;
    intConfig.pushPull   = true;

    if (!magnetometer.configureInterrupt(intConfig)) {
        return false;
    }

    if (!magnetometer.enableInterrupt(true)) {
        return false;
    }

    // Clear anything the chip latched before we were watching, so the first
    // edge the test counts is one it actually produced during the window.
    bool ready = false;
    (void) magnetometer.getDataReady(ready);

    mEdges.store(0, std::memory_order_relaxed);
    (void) mDataReady.take(0);

    mpIntPin->init(Interface::IGpi::Pull::DOWN,
                   Interface::IExti::Edge::RISING,
                   skIrqPriority);
    mpIntPin->setExtiHandler(this);
    mpIntPin->clearInt();
    mpIntPin->enableInt(true);

    // From here the pin points at this object, so teardown is required.
    mPinArmed = true;

    return magnetometer.setPowerMode(Hardware::BMM350::PowerMode::NORMAL);
}

void Magnetometer::stopStream(Hardware::BMM350& magnetometer)
{
    releasePin();

    magnetometer.enableInterrupt(false);
    magnetometer.setPowerMode(Hardware::BMM350::PowerMode::SUSPEND);
}

void Magnetometer::releasePin()
{
    // Only give back a pin this test armed.
    if (mpIntPin == nullptr || !mPinArmed) {
        return;
    }

    mPinArmed = false;

    mpIntPin->enableInt(false);
    mpIntPin->setExtiHandler(nullptr);
    mpIntPin->deinit();

    // Leave the pin as the boot configuration had it, rather than in the
    // analog state HAL_GPIO_DeInit() drops it into.
    mpIntPin->init(Interface::IGpi::Pull::NO_PULL);
}

bool Magnetometer::waitSample(Hardware::BMM350& magnetometer,
        Hardware::BMM350::Sample& sample, uint32_t timeoutMs)
{
    if (!mDataReady.take(timeoutMs)) {
        return false;
    }

    // Reading the status releases the latched line.
    bool ready = false;
    if (!magnetometer.getDataReady(ready)) {
        return false;
    }

    if (!ready) {
        return false;
    }

    return magnetometer.readSample(sample);
}

void Magnetometer::extiHandler(Interface::IExti* pExti,
        Interface::IExti::Edge edge)
{
    (void) pExti;
    (void) edge;

    mEdges.fetch_add(1, std::memory_order_relaxed);

    // IExti::Handler does not say which context this runs in, and the wrong
    // give() is a fault either way, so ask.
    if (xPortIsInsideInterrupt()) {
        BaseType_t woken = pdFALSE;
        mDataReady.giveFromISR(&woken);
        portYIELD_FROM_ISR(woken);
    } else {
        mDataReady.give();
    }
}

}   // namespace ProdTest
