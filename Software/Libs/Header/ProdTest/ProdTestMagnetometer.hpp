/**
 ******************************************************************************
 * @file    ProdTestMagnetometer.hpp
 * @brief   Production test for the Magnetometer.
 *
 *          Does the part work and is it wired up. The probe covers the chip
 *          and the bus and needs no interrupt line; --int covers the routing
 *          on its own. Calibration is not a hardware test and is not here.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef PROD_TEST_MAGNETOMETER_HPP
#define PROD_TEST_MAGNETOMETER_HPP

#include "ProdTest/IProdTest.hpp"
#include "Hardware/BMM350/BMM350.hpp"
#include "IGpio.hpp"
#include "OS.hpp"

#include <atomic>

namespace ProdTest {

class Magnetometer : public Interface::IProdTest,
                     public Interface::IExti::Handler {
public:
    Magnetometer(const std::string& id);
    ~Magnetometer() override;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    //// IExti::Handler
    void extiHandler(Interface::IExti* pExti,
                     Interface::IExti::Edge edge) override;

    /// Presence and conversion, over I2C only.
    bool runProbe(StringLogger& slog, Hardware::BMM350& magnetometer);

    /// Run the part for a window and judge the line by what arrives: too few
    /// samples, or far more edges than the configured rate can account for.
    bool runInterrupt(StringLogger& slog, Hardware::BMM350& magnetometer);

    /// Put the part in normal mode with the data-ready line armed.
    bool startStream(Hardware::BMM350& magnetometer);

    /// Disarm the line and return the part to suspend.
    void stopStream(Hardware::BMM350& magnetometer);

    /// Disarm the interrupt pin and give back the handler. The destructor
    /// calls it too, so a missed stopStream() cannot leave the pin calling
    /// into freed memory.
    void releasePin();

    /// Wait for one data-ready, release the latch and read the sample.
    bool waitSample(Hardware::BMM350& magnetometer,
                    Hardware::BMM350::Sample& sample, uint32_t timeoutMs);

    Interface::IExti* mpIntPin = nullptr;

    /// Whether the pin has been given a handler pointing at this object.
    bool mPinArmed = false;

    /// Given by the interrupt handler, taken by waitSample().
    OS::Semaphore mDataReady { 1, 0 };

    /// Edges seen by the handler. Kept apart from samples read to tell the
    /// faults apart: no edges is routing, edges without samples is the read
    /// path, far too many edges is a line firing on something else.
    std::atomic<uint32_t> mEdges { 0 };

    /// The rate the data-ready check runs at, and the averaging that goes
    /// with it. skSamplePeriodMs has to agree with skOdr.
    static constexpr Hardware::BMM350::Odr skOdr =
            Hardware::BMM350::Odr::ODR_25_HZ;
    static constexpr Hardware::BMM350::Averaging skAveraging =
            Hardware::BMM350::Averaging::AVG_4;

    /// 25 Hz, matching skOdr.
    static constexpr uint32_t skSamplePeriodMs = 40;

    /// EXTI priority. The ceiling FreeRTOS allows a handler to call its
    /// ...FromISR API from, which the handler here does.
    static constexpr uint32_t skIrqPriority = 5;

    /// Window the --int check counts over.
    static constexpr uint32_t skIntWindowMs = 1000;

    /// Samples the nominal window should yield. Only the thresholds are built
    /// on it; the verdict uses the time the loop actually took.
    static constexpr uint32_t skIntExpectedSamples =
            skIntWindowMs / skSamplePeriodMs;

    /// Below this the check fails: room for a busy bus, not for a line that
    /// barely works.
    static constexpr uint32_t skIntMinSamples = skIntExpectedSamples * 3 / 5;

    /// Above this multiple of the expectation the line is firing on something
    /// other than data-ready. Wide because the loop overshoots its window.
    static constexpr uint32_t skIntMaxEdgeFactor = 3;

    /// Generous wait for the next edge, so a slow bus does not fail the test.
    /// Bounds only the wait, not the reads that follow an edge.
    static constexpr uint32_t skSampleTimeoutMs = 6 * skSamplePeriodMs;
};

}   // namespace ProdTest

#endif // PROD_TEST_MAGNETOMETER_HPP
