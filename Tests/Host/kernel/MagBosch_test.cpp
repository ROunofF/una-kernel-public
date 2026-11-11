/**
 ******************************************************************************
 * @file    MagBosch_test.cpp
 * @brief   Host tests for the magnetometer component: lifecycle order, what
 *          streaming refuses, how a rate resolves onto the part's ladder, what
 *          reaches the callback, and how samples are timed.
 *
 *          Driven as on the target: a real worker thread and command queue,
 *          over a register-level fake bus and a fake pin that raises the
 *          interrupt. Recovery escalation is tested as a decision in
 *          MagRecoveryPolicy_test.cpp.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include "OS.hpp"
#include "System.hpp"

#include "MagBosch.hpp"
#include "MagRecoveryPolicy.hpp"

namespace {

// Every BMM350 read answers with two pad bytes before the register content.
constexpr uint16_t kDummyBytes = 2;

// -----------------------------------------------------------------------------
// A register-level BMM350 bus seeded so the SensorAPI bring-up succeeds: a
// matching chip id and an OTP controller reporting every command done.
// -----------------------------------------------------------------------------
class FakeI2c : public Interface::II2c
{

public:

    FakeI2c()
    {
        regs[BMM350_REG_CHIP_ID]        = Hardware::BMM350::skChipId;
        regs[BMM350_REG_OTP_STATUS_REG] = 0x11;   // command done, no error
    }

    uint8_t  regs[256] = {};
    bool     ready     = true;

    uint32_t readCalls  = 0;
    uint32_t writeCalls = 0;

    /// Every write refused, as a part that has stopped answering.
    bool     failWrites = false;

    bool init() override      { return true; }
    void deinit() override    {}
    bool isBusBusy() override { return false; }
    void setCpltHandler(Interface::II2c::CpltHandler*) override {}

    bool isDeviceReady(uint8_t) override { return ready; }

    bool transmit(uint8_t, const uint8_t*, uint16_t, bool = true) override
    {
        return false;
    }

    bool receive(uint8_t, uint8_t*, uint16_t, bool = true) override
    {
        return false;
    }

    bool writeMem(uint8_t, uint16_t memAddr, uint8_t, const uint8_t* pData,
                  uint16_t size, bool = true) override
    {
        writeCalls++;

        if (failWrites) {
            return false;
        }

        for (uint16_t i = 0; i < size; i++) {
            const uint8_t reg   = (memAddr + i) & 0xFFu;
            const uint8_t value = pData[i];

            regs[reg] = value;

            if (reg == BMM350_REG_PMU_CMD) {
                applyPmuCmd(value);
            }
        }

        return true;
    }

    bool readMem(uint8_t, uint16_t memAddr, uint8_t, uint8_t* pData,
                 uint16_t size, bool = true) override
    {
        readCalls++;

        for (uint16_t i = 0; i < size; i++) {
            pData[i] = (i < kDummyBytes)
                     ? 0xAAu
                     : regs[(memAddr + i - kDummyBytes) & 0xFFu];
        }

        if (memAddr == BMM350_REG_INT_STATUS) {
            // A latched line: the status read clears it, as on the part.
            if (setLine) {
                regs[BMM350_REG_INT_STATUS] &=
                        static_cast<uint8_t>(~BMM350_DRDY_DATA_REG_MSK);
                setLine(false);
            }

            if (afterStatusRead) {
                const std::function<void()> once = afterStatusRead;
                afterStatusRead = nullptr;
                once();
            }
        }

        return true;
    }

    /// ODR field last programmed: the only place the resolved rung is visible.
    uint8_t appliedOdr() const
    {
        return regs[BMM350_REG_PMU_CMD_AGGR_SET] & BMM350_ODR_MSK;
    }

    /// The chip reporting a conversion ready (the flag is not the low bit of
    /// that register). Also advances sensortime: on the part the counter is
    /// latched with the data and only moves when new data is generated.
    void setDataReady(bool isReady)
    {
        if (isReady) {
            regs[BMM350_REG_INT_STATUS] |= BMM350_DRDY_DATA_REG_MSK;

            if (setLine) {
                setLine(true);
            }

            sensorTime = (sensorTime + sensorTimeStep) & 0x00FFFFFFu;

            regs[BMM350_REG_SENSORTIME_XLSB] =
                    static_cast<uint8_t>(sensorTime & 0xFFu);
            regs[BMM350_REG_SENSORTIME_LSB] =
                    static_cast<uint8_t>((sensorTime >> 8) & 0xFFu);
            regs[BMM350_REG_SENSORTIME_MSB] =
                    static_cast<uint8_t>((sensorTime >> 16) & 0xFFu);
        } else {
            regs[BMM350_REG_INT_STATUS] &=
                    static_cast<uint8_t>(~BMM350_DRDY_DATA_REG_MSK);
        }
    }

    /// Counter value and counts per conversion (64 at 400 Hz, 256 at 100 Hz).
    uint32_t sensorTime     = 0;
    uint32_t sensorTimeStep = 64;

    /// Called as the part is told to convert, before the start-up wait ends:
    /// what the part does while the worker is still inside the switch.
    std::function<void()> onNormalMode;

    /// Set, the part drives a latched line through it: raised with a
    /// conversion, lowered by the status read.
    std::function<void(bool)> setLine;

    /// Called once, right after the next status read: what the part does
    /// just after the worker looked.
    std::function<void()> afterStatusRead;

    bool inNormalMode() const
    {
        return (regs[BMM350_REG_PMU_CMD_STATUS_0] &
                BMM350_PWR_MODE_IS_NORMAL_MSK) != 0;
    }

private:

    /// A PMU_CMD write is echoed into PMU_CMD_STATUS_0: the SensorAPI reads it
    /// back to check the command took, and the magnetic reset stops otherwise.
    void applyPmuCmd(uint8_t cmd)
    {
        uint8_t status = static_cast<uint8_t>(
                (cmd << BMM350_PMU_CMD_VALUE_POS) & BMM350_PMU_CMD_VALUE_MSK);

        if (cmd == BMM350_PMU_CMD_NM) {
            status |= BMM350_PWR_MODE_IS_NORMAL_MSK;

            if (onNormalMode) {
                onNormalMode();
            }
        }

        regs[BMM350_REG_PMU_CMD_STATUS_0] = status;
    }

};

// -----------------------------------------------------------------------------
// A pin that records what was asked of it and raises an interrupt on demand.
// -----------------------------------------------------------------------------
class FakeExti : public Interface::IExti
{

public:

    Interface::IExti::Handler* pHandler = nullptr;

    bool     level      = false;
    bool     intEnabled = false;
    bool     claimed    = false;   ///< init() with an edge was called
    uint32_t clears     = 0;

    bool init(Interface::IGpi::Pull) override { return true; }

    bool init(Interface::IGpo::Type, bool, bool = false) override
    {
        return true;
    }

    bool init(Interface::IGpi::Pull, Interface::IExti::Edge, uint32_t) override
    {
        claimed = true;
        return true;
    }

    bool deinit() override { claimed = false; return true; }
    bool get() override    { return level; }
    bool set(bool) override    { return true; }
    bool toggle() override     { return true; }

    bool enableInt(bool enable) override { intEnabled = enable; return true; }
    void clearInt() override             { clears++; }

    void setExtiHandler(Interface::IExti::Handler* p) override { pHandler = p; }

    void fire()
    {
        if (pHandler != nullptr) {
            pHandler->extiHandler(this, Interface::IExti::Edge::RISING);
        }
    }

};

class Sink : public Interface::IMagnetometer::DataCallback
{

public:

    void onMagnetic(uint64_t timestampUs, float x, float y, float z, uint32_t periodUs) override
    {
        count++;
        lastUs = timestampUs;
        lastPeriodUs = periodUs;
        lastX  = x;
        lastY  = y;
        lastZ  = z;

        stamps.push_back(timestampUs);
    }

    /// Every instant delivered, in order, so intervals can be checked.
    std::vector<uint64_t> stamps;

    uint32_t count  = 0;
    uint64_t lastUs = 0;
    uint32_t lastPeriodUs = 0;
    float    lastX  = 0.0f;
    float    lastY  = 0.0f;
    float    lastZ  = 0.0f;

};

/// Waits until the worker has taken everything queued to it and waits on an
/// empty queue again, so what its turns did, or did not do, is checked
/// directly afterwards. One worker at a time: the probe counts every queue.
/// The budget only matters when something is broken. It spins rather than
/// sleeps, since a sleep lasts a whole scheduler tick on some hosts.
bool settle(uint32_t budgetMs = 2000)
{
    const uint64_t pushed   = OS::QueueProbe::pushes();
    const auto     deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(budgetMs);

    while (OS::QueueProbe::drainedAt() < pushed) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }

        std::this_thread::yield();
    }

    return true;
}

/// A component bound to its fakes; bring() also initialises it.
struct Rig {
    FakeI2c  bus;
    FakeExti pin;
    MagBosch mag;

    bool bind()  { return mag.config(bus, pin); }
    bool bring() { return bind() && mag.init(); }
};

} // namespace

// -----------------------------------------------------------------------------
// Lifecycle order
// -----------------------------------------------------------------------------

TEST(MagBoschLifecycle, RefusesToInitBeforeItIsConfigured)
{
    MagBosch mag;

    EXPECT_FALSE(mag.init());
    EXPECT_FALSE(mag.isInit());
}

// Binding twice would silently replace a bus another caller is already using.
TEST(MagBoschLifecycle, RefusesASecondConfig)
{
    Rig rig;

    EXPECT_TRUE(rig.bind());
    EXPECT_FALSE(rig.mag.config(rig.bus, rig.pin));
}

TEST(MagBoschLifecycle, ComesUpAgainstAPartThatAnswers)
{
    Rig rig;

    ASSERT_TRUE(rig.bind());
    EXPECT_TRUE(rig.mag.init());
    EXPECT_TRUE(rig.mag.isInit());

    // init() claims the pin but leaves it masked until a stream is asked for.
    EXPECT_TRUE(rig.pin.claimed);
    EXPECT_FALSE(rig.pin.intEnabled);
}

TEST(MagBoschLifecycle, FailsWhenNothingAnswersOnTheBus)
{
    Rig rig;
    rig.bus.ready = false;

    ASSERT_TRUE(rig.bind());
    EXPECT_FALSE(rig.mag.init());
    EXPECT_FALSE(rig.mag.isInit());

    // A part that was never found is not a pin this object may keep.
    EXPECT_FALSE(rig.pin.claimed);
}

// Callers each bring the sensor up for their own reasons; a second init() must
// not repeat the bring-up and its mandated delays.
TEST(MagBoschLifecycle, InitIsIdempotent)
{
    Rig rig;

    ASSERT_TRUE(rig.bring());

    const uint32_t transfers = rig.bus.readCalls + rig.bus.writeCalls;

    EXPECT_TRUE(rig.mag.init());
    EXPECT_EQ(rig.bus.readCalls + rig.bus.writeCalls, transfers)
            << "a second init() went back to the part";
}

TEST(MagBoschLifecycle, DeinitPutsThePartDownAndGivesThePinBack)
{
    Rig rig;

    ASSERT_TRUE(rig.bring());

    rig.mag.deinit();

    EXPECT_FALSE(rig.mag.isInit());
    EXPECT_FALSE(rig.pin.claimed);
}

// Nothing to take down, and no pin this object never owned to reconfigure.
TEST(MagBoschLifecycle, DeinitWithoutInitTouchesNothing)
{
    Rig rig;

    ASSERT_TRUE(rig.bind());

    const uint32_t transfers = rig.bus.readCalls + rig.bus.writeCalls;

    rig.mag.deinit();

    EXPECT_EQ(rig.bus.readCalls + rig.bus.writeCalls, transfers);
    EXPECT_FALSE(rig.pin.claimed);
}

// -----------------------------------------------------------------------------
// What the streaming calls refuse
// -----------------------------------------------------------------------------

// Accepting would promise a stream an uninitialised part cannot deliver.
TEST(MagBoschStreaming, EveryRequestIsRefusedBeforeInit)
{
    Rig rig;

    ASSERT_TRUE(rig.bind());

    EXPECT_FALSE(rig.mag.requestStart(25.0f));
    EXPECT_FALSE(rig.mag.requestStop());
    EXPECT_FALSE(rig.mag.isRunning());
}

TEST(MagBoschStreaming, StartsAndStops)
{
    Rig rig;

    ASSERT_TRUE(rig.bring());
    ASSERT_FALSE(rig.mag.isRunning());

    EXPECT_TRUE(rig.mag.requestStart(25.0f));
    ASSERT_TRUE(settle());
    EXPECT_TRUE(rig.mag.isRunning());
    EXPECT_TRUE(rig.pin.intEnabled);

    EXPECT_TRUE(rig.mag.requestStop());
    ASSERT_TRUE(settle());
    EXPECT_FALSE(rig.mag.isRunning());
    EXPECT_FALSE(rig.pin.intEnabled);
}

// Rebuilding would lose samples while the chip is suspended and the pin
// disarmed, for a change the part applies while running.
TEST(MagBoschStreaming, AStartOnARunningStreamOnlyChangesTheRate)
{
    Rig rig;

    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.mag.requestStart(25.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    const uint32_t clears = rig.pin.clears;

    ASSERT_TRUE(rig.mag.requestStart(100.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.bus.appliedOdr() == BMM350_DATA_RATE_100HZ);

    EXPECT_TRUE(rig.mag.isRunning()) << "the stream was taken down and rebuilt";
    EXPECT_EQ(rig.pin.clears, clears) << "the pin was re-armed, so it was";
}

/// For tests that move the shared clock: each starts from zero and hands zero
/// back, even when an assertion ends it early.
class MagBoschClock : public ::testing::Test
{
protected:
    void SetUp() override    { Driver::System::SetTimeMs(0); }
    void TearDown() override { Driver::System::SetTimeMs(0); }
};

// Edges with no conversion behind them keep the worker's queue busy; the stall
// must still be noticed. The frozen clock moves only here, so the escalation
// runs in the time it takes to deliver the edges.
TEST_F(MagBoschClock, EdgesWithoutSamplesStillGetTheStreamRebuilt)
{
    Rig rig;

    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.mag.requestStart(25.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    rig.bus.setDataReady(false);

    const uint32_t writes = rig.bus.writeCalls;

    // Past the first wait (1000 ms) and the doubled second (2000 ms).
    for (uint32_t ms = 0; ms < 3500; ms += 100) {
        const uint32_t reads = rig.bus.readCalls;

        Driver::System::AdvanceMs(100);
        rig.pin.fire();

        ASSERT_TRUE(settle());
        ASSERT_GT(rig.bus.readCalls, reads)
                << "the edge at " << ms << " ms was not handled";
    }

    // A handled edge with nothing ready only reads; a rebuild writes.
    EXPECT_GT(rig.bus.writeCalls, writes)
            << "the stall went unnoticed while edges kept arriving";
}

// A part that stops answering gets a bounded number of attempts, then is left
// alone until someone asks for the stream again.
TEST_F(MagBoschClock, AStreamGivenUpOnComesBackWhenAskedAgain)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());
    rig.mag.setDataCallback(&used);
    ASSERT_TRUE(rig.mag.requestStart(25.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    rig.bus.setDataReady(false);
    rig.bus.failWrites = true;

    // One worker turn per pass, each past the longest wait there is, and a
    // pass or two more than the attempts allowed.
    for (uint32_t pass = 0; pass < MagRecoveryPolicy::skMaxAttempts + 2; pass++) {
        Driver::System::AdvanceMs(9000);
        rig.pin.fire();
        ASSERT_TRUE(settle());
    }

    EXPECT_FALSE(rig.mag.isRunning());

    // Given up: a further turn past the wait touches the bus no more.
    const uint32_t quiet = rig.bus.readCalls + rig.bus.writeCalls;

    Driver::System::AdvanceMs(9000);
    rig.pin.fire();
    ASSERT_TRUE(settle());

    EXPECT_EQ(rig.bus.readCalls + rig.bus.writeCalls, quiet)
            << "still trying after the attempts were spent";

    rig.bus.failWrites = false;

    ASSERT_TRUE(rig.mag.requestStart(25.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning())
            << "a stream given up on did not start when asked again";

    rig.bus.setDataReady(true);
    rig.pin.fire();

    ASSERT_TRUE(settle());
    EXPECT_EQ(used.count, 1u);
}

// -----------------------------------------------------------------------------
// Resolving a rate onto the part's ladder
// -----------------------------------------------------------------------------

// The contract is 'at least this fast': 12 Hz rounds up to 12.5, not down.
TEST(MagBoschRate, AskingBetweenRungsTakesTheFasterOne)
{
    Rig rig;

    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.mag.requestStart(12.0f));

    ASSERT_TRUE(settle());
    EXPECT_TRUE(rig.bus.appliedOdr() == BMM350_DATA_RATE_12_5HZ)
            << "applied odr " << static_cast<unsigned>(rig.bus.appliedOdr());
}

TEST(MagBoschRate, AskingForARungExactlyTakesThatRung)
{
    Rig rig;

    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.mag.requestStart(25.0f));

    ASSERT_TRUE(settle());
    EXPECT_TRUE(rig.bus.appliedOdr() == BMM350_DATA_RATE_25HZ);
}

// Saturating rather than refusing is what the advertised bounds promise.
TEST(MagBoschRate, RequestsBeyondTheLadderSaturate)
{
    {
        Rig rig;
        ASSERT_TRUE(rig.bring());
        ASSERT_TRUE(rig.mag.requestStart(1000.0f));
        ASSERT_TRUE(settle());
        EXPECT_TRUE(rig.bus.appliedOdr() == BMM350_DATA_RATE_400HZ);
    }

    {
        Rig rig;
        ASSERT_TRUE(rig.bring());
        ASSERT_TRUE(rig.mag.requestStart(0.01f));
        ASSERT_TRUE(settle());
        EXPECT_TRUE(rig.bus.appliedOdr() == BMM350_DATA_RATE_1_5625HZ);
    }
}

// No caller can mean zero; the slowest rung beats refusing the stream.
TEST(MagBoschRate, ZeroIsTreatedAsTheSlowestRung)
{
    Rig rig;

    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.mag.requestStart(0.0f));

    ASSERT_TRUE(settle());
    EXPECT_TRUE(rig.bus.appliedOdr() == BMM350_DATA_RATE_1_5625HZ);
}

TEST(MagBoschRate, TheAdvertisedBoundsAreThemselvesValidRequests)
{
    Rig rig;

    ASSERT_TRUE(rig.bring());

    EXPECT_FLOAT_EQ(rig.mag.getMaxSampleRateHz(), 400.0f);
    EXPECT_FLOAT_EQ(rig.mag.getMinSampleRateHz(), 1.5625f);

    ASSERT_TRUE(rig.mag.requestStart(rig.mag.getMinSampleRateHz()));
    ASSERT_TRUE(settle());
    EXPECT_TRUE(rig.bus.appliedOdr() == BMM350_DATA_RATE_1_5625HZ);

    ASSERT_TRUE(rig.mag.requestStart(rig.mag.getMaxSampleRateHz()));
    ASSERT_TRUE(settle());
    EXPECT_TRUE(rig.bus.appliedOdr() == BMM350_DATA_RATE_400HZ);
}

// -----------------------------------------------------------------------------
// The callback
// -----------------------------------------------------------------------------

TEST(MagBoschCallbacks, DeliversAMeasurementToTheSubscriber)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());

    rig.mag.setDataCallback(&used);

    ASSERT_TRUE(rig.mag.requestStart(25.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    // A conversion completes: the chip reports data ready and the line rises.
    rig.bus.setDataReady(true);
    rig.pin.fire();

    ASSERT_TRUE(settle());
    EXPECT_EQ(used.count, 1u);
}

// Each measurement carries the period of the rung the part was set to, not
// the rate asked for: listeners decimate against it.
TEST(MagBoschCallbacks, AMeasurementCarriesThePeriodOfTheRungApplied)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());

    rig.mag.setDataCallback(&used);

    // Between 25 and 50 Hz: the faster rung.
    ASSERT_TRUE(rig.mag.requestStart(30.0f));
    ASSERT_TRUE(settle());

    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());

    ASSERT_GE(used.count, 1u);
    EXPECT_EQ(used.lastPeriodUs, 20000u);

    // A new rate reaches the next measurement, a period later.
    ASSERT_TRUE(rig.mag.requestStart(100.0f));
    ASSERT_TRUE(settle());

    Driver::System::AdvanceMs(20);
    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());

    EXPECT_EQ(used.lastPeriodUs, 10000u);

    Driver::System::SetTimeMs(0);
}

// Delivery resuming on re-attach shows the slot stopped it, not the stream.
TEST(MagBoschCallbacks, DetachingStopsDeliveryAndReattachingResumesIt)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());

    rig.mag.setDataCallback(&used);

    ASSERT_TRUE(rig.mag.requestStart(25.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    rig.mag.setDataCallback(nullptr);

    rig.bus.setDataReady(true);
    rig.pin.fire();

    // Once the worker has had the edge, delivery either happened or did not.
    ASSERT_TRUE(settle());
    EXPECT_EQ(used.count, 0u);

    rig.mag.setDataCallback(&used);

    // The next conversion, a period later.
    Driver::System::AdvanceMs(40);
    rig.bus.setDataReady(true);
    rig.pin.fire();

    ASSERT_TRUE(settle());
    EXPECT_EQ(used.count, 1u)
            << "the stream stopped rather than the slot emptying";

    Driver::System::SetTimeMs(0);
}

// -----------------------------------------------------------------------------
// The time base, as the worker feeds it; the arithmetic is MagTimeBase_test's
// -----------------------------------------------------------------------------

namespace {

/// How far an interval may sit from the period over a stream's first few
/// edges, while the rate is still read off a millisecond clock: the part's
/// tolerance, which is what the fit's prior lets the rate be off by. It is
/// headroom over the worst seen here, 21 us at 400 Hz and 87 us after a
/// change to 100 Hz, not a bound derived from the steering. How close an
/// interval settles is for the tests of a part converting on its own
/// oscillator.
constexpr double steerUs(uint32_t periodUs)
{
    return static_cast<double>(((static_cast<uint64_t>(periodUs)
                                 * MagTimeBase::skRatePriorPpm) / 1000000U) + 1U);
}

/// Start a 400 Hz stream and deliver howMany samples, one conversion each.
/// Every sample carries an edge. The clock is read as the test left it for the
/// first, then moves on 3 and 2 ms in turn, a millisecond clock keeping pace
/// with a 2.5 ms period.
bool streamSamples(Rig& rig, Sink& sink, uint32_t howMany)
{
    if (!rig.mag.requestStart(400.0f)) {
        return false;
    }

    if (!settle() || !rig.mag.isRunning()) {
        return false;
    }

    for (uint32_t i = 0; i < howMany; i++) {
        const uint32_t before = sink.count;

        if (i > 0U) {
            Driver::System::AdvanceMs(((i % 2U) != 0U) ? 3U : 2U);
        }

        rig.bus.setDataReady(true);
        rig.pin.fire();

        if (!settle() || (sink.count != (before + 1U))) {
            return false;
        }
    }

    return true;
}

} // namespace

/// Every test here starts on a clock that has been running, whatever the one
/// before it left behind, and hands back the zero the other suites expect.
class MagBoschTimeBase : public ::testing::Test
{
protected:
    void SetUp() override    { Driver::System::SetTimeMs(1000); }
    void TearDown() override { Driver::System::SetTimeMs(0); }
};

// 64 counts of 39.0625 us per conversion at 400 Hz: 2500 us apart whenever
// the worker got round to reading them, though the edges are stamped on a
// clock that only counts whole milliseconds.
TEST_F(MagBoschTimeBase, IntervalsComeFromThePartNotTheClock)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());
    rig.mag.setDataCallback(&used);

    ASSERT_TRUE(streamSamples(rig, used, 8));
    ASSERT_GE(used.stamps.size(), 8u);

    for (size_t i = 1; i < used.stamps.size(); i++) {
        EXPECT_NEAR(static_cast<double>(used.stamps[i] - used.stamps[i - 1]),
                    2500.0, steerUs(2500U))
                << "step " << i << " of " << used.stamps.size();
    }
}

// The counter keeps its value across a stop, so a restart must re-anchor
// rather than continue where the old stream left off.
TEST_F(MagBoschTimeBase, ARestartAnchorsAfresh)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());
    rig.mag.setDataCallback(&used);

    ASSERT_TRUE(streamSamples(rig, used, 4));
    ASSERT_TRUE(rig.mag.requestStop());
    ASSERT_TRUE(settle());
    ASSERT_FALSE(rig.mag.isRunning());

    Driver::System::SetTimeMs(5000);

    used.stamps.clear();

    ASSERT_TRUE(streamSamples(rig, used, 4));
    ASSERT_FALSE(used.stamps.empty());

    EXPECT_EQ(used.stamps[0], 4999000ull)
            << "the new stream did not take the clock it started on";
}

// The part converts while the switch to normal mode waits out its start-up,
// and the edge it raises then is served only after the wait, when the read
// takes a later conversion. That edge must not place the stream: the first
// sample is the one announced after the start, stamped by its own edge.
TEST_F(MagBoschTimeBase, AnEdgeRaisedWhileThePartStartsIsNotUsed)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());
    rig.mag.setDataCallback(&used);

    rig.bus.sensorTimeStep = 256;
    rig.bus.onNormalMode   = [&rig]() {
        rig.bus.setDataReady(true);
        rig.pin.fire();
    };

    ASSERT_TRUE(rig.mag.requestStart(100.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    rig.bus.onNormalMode = nullptr;

    Driver::System::AdvanceMs(20);
    const uint32_t edgeMs = Driver::System::GetTimeMs();

    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());

    ASSERT_EQ(used.count, 1u) << "a sample was read on the edge raised during the start-up";
    EXPECT_EQ(used.stamps[0], (static_cast<uint64_t>(edgeMs) * 1000U) - 1000U)
            << "the first sample was not placed by its own edge";
}

// Only the first sample is placed by its edge, a millisecond below the one the
// edge was seen in; later ones are counted from it by the part's clock, hence
// a single edge.
TEST_F(MagBoschClock, SamplesAreStampedOnTheSystemTimeBase)
{
    Sink used;
    Rig  rig;

    Driver::System::SetTimeMs(1234);

    ASSERT_TRUE(rig.bring());
    rig.mag.setDataCallback(&used);

    ASSERT_TRUE(rig.mag.requestStart(25.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    rig.bus.setDataReady(true);
    rig.pin.fire();

    ASSERT_TRUE(settle());
    ASSERT_EQ(used.count, 1u);

    EXPECT_EQ(used.lastUs, 1233000ull);
}

// The start releases a line latched during the part's start-up wait, and only
// after the stream's epoch has moved on, so an edge raised right after the
// release belongs to the stream. The line is latched: a conversion holds it
// high until the status is read. Released first, or not at all, the stream
// would wait on a line that never rises again.
TEST_F(MagBoschTimeBase, TheStartLeavesNoLatchedLineBehind)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());
    rig.mag.setDataCallback(&used);

    rig.bus.sensorTimeStep = 256;
    rig.bus.setLine        = [&rig](bool high) { rig.pin.level = high; };
    rig.bus.onNormalMode   = [&rig]() {
        // A conversion during the wait, latching the line, and its edge.
        rig.bus.setDataReady(true);
        rig.pin.fire();

        // Another completes just after the start looks at the status.
        rig.bus.afterStatusRead = [&rig]() {
            Driver::System::AdvanceMs(3);
            rig.bus.setDataReady(true);
            rig.pin.fire();
        };
    };

    ASSERT_TRUE(rig.mag.requestStart(100.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    rig.bus.onNormalMode = nullptr;

    EXPECT_EQ(used.count, 1u) << "the conversion after the start was not delivered";
    EXPECT_FALSE(rig.pin.level) << "the line was left latched";
}

// A stream's first sample places it in time. Read long after its edge, it is
// a later conversion than the edge announced, so it is not delivered; the next
// edge places the stream.
TEST_F(MagBoschTimeBase, AFirstSampleReadLateIsNotDelivered)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());
    rig.mag.setDataCallback(&used);

    rig.bus.sensorTimeStep = 256;

    ASSERT_TRUE(rig.mag.requestStart(100.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    // The worker is held up for 15 ms, a period and a half, before it reads.
    rig.bus.afterStatusRead = []() { Driver::System::AdvanceMs(15); };
    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());

    EXPECT_EQ(used.count, 0u) << "a sample read late placed the stream";

    Driver::System::AdvanceMs(5);
    const uint32_t edgeMs = Driver::System::GetTimeMs();

    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());

    ASSERT_EQ(used.count, 1u);
    EXPECT_GE(used.stamps[0], (static_cast<uint64_t>(edgeMs) - 1U) * 1000U)
            << "the stream was not placed by the next edge";
    EXPECT_LE(used.stamps[0], static_cast<uint64_t>(edgeMs) * 1000U);
}

// At 100 Hz a first sample read up to half a period and a millisecond after
// its edge, 6 ms, is its edge's conversion and places the stream; one read
// 7 ms after may already be the next, and does not.
TEST_F(MagBoschTimeBase, AFirstSampleIsPlacedUpToHalfAPeriodAndATickLate)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());
    rig.mag.setDataCallback(&used);

    rig.bus.sensorTimeStep = 256;

    ASSERT_TRUE(rig.mag.requestStart(100.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    rig.bus.afterStatusRead = []() { Driver::System::AdvanceMs(7); };
    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());

    EXPECT_EQ(used.count, 0u) << "a sample read 7 ms after its edge placed the stream";

    Driver::System::AdvanceMs(3);

    rig.bus.afterStatusRead = []() { Driver::System::AdvanceMs(6); };
    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());

    EXPECT_EQ(used.count, 1u) << "a sample read 6 ms after its edge did not place the stream";
}

// Each stream is placed afresh: after a stop and a start, a first sample read
// late is not delivered, however well the stream before was placed.
TEST_F(MagBoschTimeBase, EachStreamIsPlacedAfresh)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());
    rig.mag.setDataCallback(&used);

    rig.bus.sensorTimeStep = 256;

    ASSERT_TRUE(rig.mag.requestStart(100.0f));
    ASSERT_TRUE(settle());

    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());
    ASSERT_EQ(used.count, 1u);

    ASSERT_TRUE(rig.mag.requestStop());
    ASSERT_TRUE(settle());
    Driver::System::AdvanceMs(100);
    ASSERT_TRUE(rig.mag.requestStart(100.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    rig.bus.afterStatusRead = []() { Driver::System::AdvanceMs(15); };
    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());

    EXPECT_EQ(used.count, 1u) << "the new stream was placed by a sample read late";
}

// The drain loop reads the status, releasing the latch, then the sample. A
// conversion completing between the two raises the line again after the
// sample read already took it, so the loop reads the same conversion once
// more. It is delivered once.
TEST_F(MagBoschTimeBase, AConversionReadTwiceIsDeliveredOnce)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());
    rig.mag.setDataCallback(&used);

    rig.bus.sensorTimeStep = 64;
    rig.bus.setLine        = [&rig](bool high) { rig.pin.level = high; };

    ASSERT_TRUE(rig.mag.requestStart(400.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());
    ASSERT_EQ(used.count, 1u);

    // The next conversion is overtaken by another between the status and
    // the sample read, which raises its own edge too; the read comes two
    // periods after the first.
    Driver::System::AdvanceMs(5);
    rig.bus.setDataReady(true);
    rig.bus.afterStatusRead = [&rig]() {
        rig.bus.setDataReady(true);
        rig.pin.fire();
    };
    rig.pin.fire();
    ASSERT_TRUE(settle());

    // The overtaken conversion is lost, the part having no FIFO; the one
    // that overtook it comes once, two conversions after the first.
    ASSERT_EQ(used.count, 2u) << "the conversion read twice was delivered twice";
    EXPECT_NEAR(static_cast<double>(used.stamps[1] - used.stamps[0]),
                5000.0, steerUs(2500U));
}

// The part's count may start again with a new stream. The first sample of
// a stream is delivered even if its count equals the last one delivered
// before the stop: it places the stream, it does not repeat a conversion.
TEST_F(MagBoschTimeBase, AStreamsFirstSampleIsNotTakenForARepeat)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());
    rig.mag.setDataCallback(&used);

    rig.bus.sensorTimeStep = 256;

    ASSERT_TRUE(rig.mag.requestStart(100.0f));
    ASSERT_TRUE(settle());

    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());
    ASSERT_EQ(used.count, 1u);

    ASSERT_TRUE(rig.mag.requestStop());
    ASSERT_TRUE(settle());
    Driver::System::AdvanceMs(100);

    // The counter starts again, so the next count equals the last one.
    rig.bus.sensorTime = 0;

    ASSERT_TRUE(rig.mag.requestStart(100.0f));
    ASSERT_TRUE(settle());
    ASSERT_TRUE(rig.mag.isRunning());

    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());

    EXPECT_EQ(used.count, 2u) << "a new stream's first sample was taken for a repeat";
}

// A sample the time base withholds, having no instant both after the last one
// and no later than its read, is not delivered; the stream carries on.
TEST_F(MagBoschTimeBase, AWithheldSampleIsNotDelivered)
{
    Sink used;
    Rig  rig;

    ASSERT_TRUE(rig.bring());
    rig.mag.setDataCallback(&used);

    rig.bus.sensorTimeStep = 256;

    ASSERT_TRUE(rig.mag.requestStart(100.0f));
    ASSERT_TRUE(settle());

    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());
    ASSERT_EQ(used.count, 1u);

    // The clock steps back by 5 ms: the next conversion has no instant.
    Driver::System::SetTimeMs(Driver::System::GetTimeMs() - 5U);
    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());
    EXPECT_EQ(used.count, 1u) << "a sample with no instant was delivered";

    // Later, instants resume and follow the last one delivered.
    Driver::System::AdvanceMs(30);
    rig.bus.setDataReady(true);
    rig.pin.fire();
    ASSERT_TRUE(settle());
    ASSERT_EQ(used.count, 2u);
    EXPECT_GT(used.stamps[1], used.stamps[0]);
}
