/**
 ******************************************************************************
 * @file    BaroMs5837_test.cpp
 * @brief   Host tests for the barometer component: lifecycle order, what
 *          streaming refuses, how a rate resolves onto the ladder, what
 *          reaches the callback and with which instant, when the supply is
 *          on, and how a part that stops measuring is recovered or given up.
 *
 *          Driven as on the target: a real worker thread and command queue,
 *          over a scripted part on a fake bus and a fake supply pin. The
 *          clock does not run: a test moves it and then sends the worker a
 *          request that changes nothing, which is what makes it look at the
 *          clock again. The ladder, the grid and the recovery decision are
 *          tested as arithmetic in BaroCadence_test.cpp and
 *          BaroRecoveryPolicy_test.cpp.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "OS.hpp"
#include "System.hpp"

#include "Barometer/BaroMs5837.hpp"

#include "FakeMs5837.hpp"

namespace {

// -----------------------------------------------------------------------------
// The supply pin: records its level and how often the supply came on.
// -----------------------------------------------------------------------------
class FakePin : public Interface::IGpo
{

public:

    bool     level   = false;
    uint32_t rises   = 0;      ///< Times the level went from low to high.

    /// The same count, for a test that reads it while the worker runs.
    std::atomic<uint32_t> risesSeen { 0 };
    uint32_t inits   = 0;
    uint32_t deinits = 0;

    bool init(Interface::IGpo::Type, bool initState, bool = false) override
    {
        inits++;
        apply(initState);
        return true;
    }

    bool deinit() override { deinits++; return true; }
    bool get() override    { return level; }

    bool set(bool value) override
    {
        apply(value);
        return true;
    }

    bool toggle() override
    {
        apply(!level);
        return true;
    }

private:

    void apply(bool value)
    {
        if (value && !level) {
            rises++;
            risesSeen.fetch_add(1, std::memory_order_release);
        }

        level = value;
    }

};

class Sink : public Interface::IBarometer::DataCallback
{

public:

    void onPressure(uint64_t timestampUs, float mbar, uint32_t periodUs) override
    {
        pressures++;
        pressuresSeen.fetch_add(1, std::memory_order_release);
        lastPressureUs = timestampUs;
        lastMbar       = mbar;
        lastPeriodUs   = periodUs;

        stamps.push_back(timestampUs);
    }

    void onTemperature(uint64_t timestampUs, float degC, uint32_t periodUs) override
    {
        temperatures++;
        lastTemperatureUs     = timestampUs;
        lastDegC              = degC;
        lastTemperaturePeriod = periodUs;
    }

    /// The pressure count, for a test that reads it while the worker runs.
    std::atomic<uint32_t> pressuresSeen { 0 };

    /// Every pressure instant delivered, in order.
    std::vector<uint64_t> stamps;

    uint32_t pressures             = 0;
    uint32_t temperatures          = 0;
    uint64_t lastPressureUs        = 0;
    uint64_t lastTemperatureUs     = 0;
    uint32_t lastPeriodUs          = 0;
    uint32_t lastTemperaturePeriod = 0;
    float    lastMbar              = 0.0f;
    float    lastDegC              = 0.0f;

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

/// Waits, in real time, for something the worker does with nobody asking it
/// to. The budget only matters when it never happens.
bool waitFor(const std::atomic<uint32_t>& count, uint32_t value,
             uint32_t budgetMs = 2000)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(budgetMs);

    while (count.load(std::memory_order_acquire) != value) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }

        std::this_thread::yield();
    }

    return true;
}

constexpr uint32_t kStartMs = 50000;

/// Where the millisecond clock turns over: it is the 1024 Hz tick counted in
/// milliseconds, so it returns to 0 short of 2^32.
constexpr uint32_t kClockTurnsAtMs = 4194304000u;

/// A component bound to its fakes, over a part that is present and converts.
/// The component is declared last, so its worker is gone before they are.
struct Rig {
    FakeMs5837::Bus bus;
    FakePin         pin;
    Sink            sink;
    BaroMs5837      baro;

    Rig()
    {
        bus.prom   = FakeMs5837::makeProm(FakeMs5837::kFactory02BA01);
        bus.d1Step = 7;
        bus.d2Step = 3;

        Driver::System::SetTimeMs(kStartMs);
    }

    bool bind()  { return baro.config(bus, pin); }

    bool bring()
    {
        if (!bind() || !baro.init()) {
            return false;
        }

        baro.setDataCallback(&sink);

        return true;
    }

    /// Ask for a rate and let the worker act on it.
    bool start(float rateHz)
    {
        mRateHz = rateHz;

        return baro.requestStart(rateHz) && settle();
    }

    bool stop() { return baro.requestStop() && settle(); }

    /// Let this much time pass for the worker. The request repeats the rate
    /// the stream already has: it changes nothing and makes the worker look
    /// at the clock.
    bool pass(uint32_t ms)
    {
        Driver::System::AdvanceMs(ms);

        return baro.requestStart(mRateHz) && settle();
    }

    /// A part that has stopped converting: every read returns the same words.
    void freeze()
    {
        bus.d1Step = 0;
        bus.d2Step = 0;
    }

private:

    float mRateHz = 1.0f;
};

} // namespace

// -----------------------------------------------------------------------------
// Lifecycle order
// -----------------------------------------------------------------------------

TEST(BaroMs5837Lifecycle, RefusesToInitBeforeItIsConfigured)
{
    BaroMs5837 baro;

    EXPECT_FALSE(baro.init());
    EXPECT_FALSE(baro.isInit());
}

// Binding twice would silently replace a bus another caller is already using.
TEST(BaroMs5837Lifecycle, RefusesASecondConfig)
{
    Rig rig;

    EXPECT_TRUE(rig.bind());
    EXPECT_FALSE(rig.bind());
}

TEST(BaroMs5837Lifecycle, InitFindsThePartAndLeavesItUnpowered)
{
    Rig rig;

    ASSERT_TRUE(rig.bring());

    EXPECT_TRUE(rig.baro.isInit());
    EXPECT_FALSE(rig.baro.isRunning());

    // Powered once to be probed, and off again.
    EXPECT_EQ(rig.pin.rises, 1u);
    EXPECT_EQ(rig.bus.resets, 1u);
    EXPECT_FALSE(rig.pin.level);
}

// A part called absent stays absent until the next boot, so it is probed more
// than once, and the supply is not left on for it.
TEST(BaroMs5837Lifecycle, InitFailsWithoutAPartAfterProbingItRepeatedly)
{
    Rig rig;
    rig.bus.ready = false;

    EXPECT_FALSE(rig.bring());

    EXPECT_FALSE(rig.baro.isInit());
    EXPECT_EQ(rig.pin.rises, BaroMs5837::skProbeAttempts);
    EXPECT_FALSE(rig.pin.level);
}

TEST(BaroMs5837Lifecycle, InitRefusesAPartItCannotConvert)
{
    Rig rig;
    rig.bus.prom = FakeMs5837::makeProm(FakeMs5837::kFactory30BA26);

    EXPECT_FALSE(rig.bring());
    EXPECT_FALSE(rig.pin.level);
}

TEST(BaroMs5837Lifecycle, ASecondInitDoesNotProbeAgain)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());

    EXPECT_TRUE(rig.baro.init());
    EXPECT_EQ(rig.pin.rises, 1u);
}

TEST(BaroMs5837Lifecycle, RequestsAreRefusedBeforeInit)
{
    Rig rig;
    ASSERT_TRUE(rig.bind());

    EXPECT_FALSE(rig.baro.requestStart(1.0f));
    EXPECT_FALSE(rig.baro.requestStop());
    EXPECT_FALSE(rig.pin.level);
}

TEST(BaroMs5837Lifecycle, DeinitStopsTheStreamAndTakesTheSupplyAway)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(1.0f));
    ASSERT_TRUE(rig.pin.level);

    rig.baro.deinit();

    EXPECT_FALSE(rig.baro.isInit());
    EXPECT_FALSE(rig.baro.isRunning());
    EXPECT_FALSE(rig.pin.level);

    // Low is the off state: an undriven pin would let the supply float.
    EXPECT_EQ(rig.pin.deinits, 0u);

    EXPECT_FALSE(rig.baro.requestStart(1.0f));
}

TEST(BaroMs5837Lifecycle, ComesUpAgainAfterADeinit)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    rig.baro.deinit();

    ASSERT_TRUE(rig.baro.init());
    ASSERT_TRUE(rig.start(1.0f));

    EXPECT_TRUE(rig.baro.isRunning());
    EXPECT_EQ(rig.sink.pressures, 1u);
}

TEST(BaroMs5837Lifecycle, DestructionMidStreamTakesTheSupplyAway)
{
    FakeMs5837::Bus bus;
    FakePin         pin;

    bus.prom   = FakeMs5837::makeProm(FakeMs5837::kFactory02BA01);
    bus.d1Step = 1;

    {
        BaroMs5837 baro;

        ASSERT_TRUE(baro.config(bus, pin));
        ASSERT_TRUE(baro.init());
        ASSERT_TRUE(baro.requestStart(1.0f));
        ASSERT_TRUE(settle());
        ASSERT_TRUE(pin.level);
    }

    EXPECT_FALSE(pin.level);
}

// -----------------------------------------------------------------------------
// The stream
// -----------------------------------------------------------------------------

TEST(BaroMs5837Stream, AStartPowersThePartAndMeasuresAtOnce)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());

    ASSERT_TRUE(rig.start(1.0f));

    EXPECT_TRUE(rig.pin.level);
    EXPECT_TRUE(rig.baro.isRunning());

    // One measurement is one sample on each channel.
    EXPECT_EQ(rig.sink.pressures, 1u);
    EXPECT_EQ(rig.sink.temperatures, 1u);
}

// Both channels carry the clock as it was when the measurement was started.
TEST(BaroMs5837Stream, BothChannelsCarryTheInstantTheMeasurementStarted)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());

    Driver::System::SetTimeMs(123456);
    ASSERT_TRUE(rig.start(1.0f));

    EXPECT_EQ(rig.sink.lastPressureUs, 123456000u);
    EXPECT_EQ(rig.sink.lastTemperatureUs, 123456000u);
}

// The conversions take tens of milliseconds, and the bus can make the wait
// longer. None of it is in the instant.
TEST(BaroMs5837Stream, TheInstantIsTakenBeforeTheConversions)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());

    rig.bus.onConversion = [] { Driver::System::AdvanceMs(25); };

    Driver::System::SetTimeMs(200000);
    ASSERT_TRUE(rig.start(1.0f));

    ASSERT_EQ(Driver::System::GetTimeMs(), 200050u);
    EXPECT_EQ(rig.sink.lastPressureUs, 200000000u);
    EXPECT_EQ(rig.sink.lastTemperatureUs, 200000000u);
}

TEST(BaroMs5837Stream, DeliversWhatThePartMeasured)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());

    float degC = 0.0f;
    float mbar = 0.0f;
    Hardware::MS5837::calculate(rig.bus.prom, rig.bus.d1, rig.bus.d2, degC, mbar);

    ASSERT_TRUE(rig.start(1.0f));

    EXPECT_FLOAT_EQ(rig.sink.lastMbar, mbar);
    EXPECT_FLOAT_EQ(rig.sink.lastDegC, degC);
}

TEST(BaroMs5837Stream, MeasuresOncePerPeriod)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));
    ASSERT_EQ(rig.sink.pressures, 1u);

    ASSERT_TRUE(rig.pass(99));
    EXPECT_EQ(rig.sink.pressures, 1u);

    ASSERT_TRUE(rig.pass(1));
    EXPECT_EQ(rig.sink.pressures, 2u);

    // Looking again at the same instant does not measure again.
    ASSERT_TRUE(rig.pass(0));
    EXPECT_EQ(rig.sink.pressures, 2u);

    ASSERT_TRUE(rig.pass(100));
    EXPECT_EQ(rig.sink.pressures, 3u);

    ASSERT_EQ(rig.sink.stamps.size(), 3u);
    EXPECT_EQ(rig.sink.stamps[1] - rig.sink.stamps[0], 100000u);
    EXPECT_EQ(rig.sink.stamps[2] - rig.sink.stamps[1], 100000u);
}

// Every other case here moves the clock and then sends a request, which is
// what makes the worker look at it. On the watch nobody sends one: the
// worker has to wake for the next measurement by itself. Here it is left
// 10 ms short of one, and only the clock is moved.
TEST(BaroMs5837Stream, WakesForTheNextMeasurementByItself)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));

    ASSERT_TRUE(rig.pass(90));
    ASSERT_EQ(rig.sink.pressuresSeen.load(), 1u);

    Driver::System::AdvanceMs(10);

    EXPECT_TRUE(waitFor(rig.sink.pressuresSeen, 2u));
    ASSERT_TRUE(settle());
}

// What a consumer counts its own rate against: the rung, not the request.
TEST(BaroMs5837Stream, AMeasurementCarriesThePeriodOfTheRungApplied)
{
    struct Case { float rateHz; uint32_t periodUs; };

    const Case cases[] = {
        { 10.0f,      100000u },
        { 5.0f,       200000u },
        { 1.0f,      1000000u },
        { 0.2f,      5000000u },
        { 0.1f,     10000000u },
        { 3.0f,       200000u },    // between rungs: the faster one
        { 500.0f,     100000u },    // past the fast end
        { 0.001f,   10000000u },    // past the slow end
    };

    for (const Case& c : cases) {
        Rig rig;
        ASSERT_TRUE(rig.bring());
        ASSERT_TRUE(rig.start(c.rateHz));

        EXPECT_EQ(rig.sink.lastPeriodUs, c.periodUs) << "rateHz=" << c.rateHz;
        EXPECT_EQ(rig.sink.lastTemperaturePeriod, c.periodUs)
                << "rateHz=" << c.rateHz;
    }
}

TEST(BaroMs5837Stream, ReportsTheEndsOfItsLadder)
{
    Rig rig;

    EXPECT_FLOAT_EQ(rig.baro.getMaxSampleRateHz(), 10.0f);
    EXPECT_FLOAT_EQ(rig.baro.getMinSampleRateHz(), 0.1f);
}

// A START on a running stream changes its rate and nothing else: the part is
// not power cycled and its calibration is not read again.
TEST(BaroMs5837Stream, ARateChangeLeavesThePartPowered)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(1.0f));

    const uint32_t rises  = rig.pin.rises;
    const uint32_t resets = rig.bus.resets;

    ASSERT_TRUE(rig.start(10.0f));
    ASSERT_TRUE(rig.pass(100));

    EXPECT_EQ(rig.sink.pressures, 2u);
    EXPECT_EQ(rig.sink.lastPeriodUs, 100000u);

    EXPECT_EQ(rig.pin.rises, rises);
    EXPECT_EQ(rig.bus.resets, resets);
    EXPECT_TRUE(rig.pin.level);
}

TEST(BaroMs5837Stream, SlowingDownHoldsTheNextMeasurementBack)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));
    ASSERT_EQ(rig.sink.pressures, 1u);

    ASSERT_TRUE(rig.start(1.0f));

    ASSERT_TRUE(rig.pass(999));
    EXPECT_EQ(rig.sink.pressures, 1u);

    ASSERT_TRUE(rig.pass(1));
    EXPECT_EQ(rig.sink.pressures, 2u);
    EXPECT_EQ(rig.sink.lastPeriodUs, 1000000u);
}

TEST(BaroMs5837Stream, AStopTakesTheSupplyAway)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(1.0f));

    ASSERT_TRUE(rig.stop());

    EXPECT_FALSE(rig.pin.level);
    EXPECT_FALSE(rig.baro.isRunning());
    EXPECT_TRUE(rig.baro.isInit());
}

TEST(BaroMs5837Stream, AStopOnAStoppedPartIsAccepted)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());

    EXPECT_TRUE(rig.stop());
    EXPECT_FALSE(rig.pin.level);
}

// A new stream is a new power-up and measures at once, whatever the old one's
// grid was.
TEST(BaroMs5837Stream, AStartAfterAStopBeginsAfresh)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(1.0f));
    ASSERT_TRUE(rig.stop());

    const uint32_t rises = rig.pin.rises;

    ASSERT_TRUE(rig.start(1.0f));

    EXPECT_EQ(rig.pin.rises, rises + 1u);
    EXPECT_EQ(rig.sink.pressures, 2u);
}

TEST(BaroMs5837Stream, RunsWithNoSubscriber)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    rig.baro.setDataCallback(nullptr);

    ASSERT_TRUE(rig.start(10.0f));
    ASSERT_TRUE(rig.pass(100));
    EXPECT_EQ(rig.sink.pressures, 0u);
    EXPECT_EQ(rig.bus.conversions, 2u);

    rig.baro.setDataCallback(&rig.sink);
    ASSERT_TRUE(rig.pass(100));
    EXPECT_EQ(rig.sink.pressures, 1u);
}

// -----------------------------------------------------------------------------
// Measurements that do not count
// -----------------------------------------------------------------------------

// Nothing rather than the last value again, and one failure is not a fault.
TEST(BaroMs5837Faults, AFailedMeasurementDeliversNothing)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));
    ASSERT_EQ(rig.sink.pressures, 1u);

    rig.bus.failAdc = true;
    ASSERT_TRUE(rig.pass(100));
    EXPECT_EQ(rig.sink.pressures, 1u);
    EXPECT_EQ(rig.sink.temperatures, 1u);

    rig.bus.failAdc = false;
    ASSERT_TRUE(rig.pass(100));
    EXPECT_EQ(rig.sink.pressures, 2u);

    EXPECT_EQ(rig.pin.rises, 2u);   // the probe and the start, no power cycle
}

// The same ADC words as the measurement before. A part that converts returns
// them now and then, so one repeat is handed on like any other measurement.
TEST(BaroMs5837Faults, ARepeatedMeasurementIsDelivered)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));
    ASSERT_EQ(rig.sink.pressures, 1u);

    // The words now on offer are fresh once, then stay.
    rig.freeze();

    ASSERT_TRUE(rig.pass(100));
    EXPECT_EQ(rig.sink.pressures, 2u);

    ASSERT_TRUE(rig.pass(100));
    ASSERT_TRUE(rig.pass(100));
    EXPECT_EQ(rig.sink.pressures, 4u);
    EXPECT_EQ(rig.sink.temperatures, 4u);
    EXPECT_EQ(rig.bus.conversions, 4u);
    EXPECT_EQ(rig.pin.rises, 2u);   // the probe and the start, no power cycle
}

// One word moving is a conversion: the pair is what is compared. Each word
// stands still for more measurements than a run of repeats is allowed, and
// the part is not power cycled.
TEST(BaroMs5837Faults, OneWordChangingIsAFreshMeasurement)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());

    rig.bus.d1Step = 5;
    rig.bus.d2Step = 0;

    ASSERT_TRUE(rig.start(10.0f));

    const uint32_t rises = rig.pin.rises;

    for (uint32_t i = 0; i <= BaroRecoveryPolicy::skMaxRepeatedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    EXPECT_EQ(rig.pin.rises, rises);

    // And the other word alone.
    rig.bus.d1Step = 0;
    rig.bus.d2Step = 5;

    for (uint32_t i = 0; i <= BaroRecoveryPolicy::skMaxRepeatedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    EXPECT_EQ(rig.pin.rises, rises);
    EXPECT_EQ(rig.sink.pressures,
              1u + (2u * (BaroRecoveryPolicy::skMaxRepeatedReads + 1u)));
}

// -----------------------------------------------------------------------------
// Recovery
// -----------------------------------------------------------------------------

TEST(BaroMs5837Recovery, APartThatStopsAnsweringIsPowerCycledAtOnce)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));

    const uint32_t rises = rig.pin.rises;

    rig.bus.failAdc = true;

    for (uint32_t i = 1; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
        EXPECT_EQ(rig.pin.rises, rises) << "after failure " << i;
    }

    ASSERT_TRUE(rig.pass(100));

    // No wait before the first power cycle, and the calibration is read again.
    EXPECT_EQ(rig.pin.rises, rises + 1u);
    EXPECT_EQ(rig.bus.resets, 3u);
    EXPECT_TRUE(rig.pin.level);
}

TEST(BaroMs5837Recovery, DeliveryResumesOnceThePartMeasuresAgain)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));
    ASSERT_EQ(rig.sink.pressures, 1u);

    rig.bus.failAdc = true;

    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    ASSERT_EQ(rig.sink.pressures, 1u);

    rig.bus.failAdc = false;
    ASSERT_TRUE(rig.pass(100));

    EXPECT_EQ(rig.sink.pressures, 2u);
    EXPECT_TRUE(rig.baro.isRunning());
}

TEST(BaroMs5837Recovery, APartFrozenOnOneValueIsPowerCycled)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));

    const uint32_t rises = rig.pin.rises;

    rig.freeze();

    // The first is still fresh; the repeats follow it.
    ASSERT_TRUE(rig.pass(100));

    for (uint32_t i = 1; i < BaroRecoveryPolicy::skMaxRepeatedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
        EXPECT_EQ(rig.pin.rises, rises) << "after repeat " << i;
    }

    ASSERT_TRUE(rig.pass(100));

    EXPECT_EQ(rig.pin.rises, rises + 1u);

    // Every measurement was handed on, the repeats with the rest.
    EXPECT_EQ(rig.sink.pressures, rig.bus.conversions);
}

// The part is unpowered while it waits for its next attempt, and the wait is
// the policy's: nothing happens a millisecond short of it.
TEST(BaroMs5837Recovery, WaitsUnpoweredBetweenAttempts)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));

    rig.bus.ready = false;

    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    // The first attempt was made at once and failed.
    const uint32_t rises = rig.pin.rises;
    EXPECT_FALSE(rig.pin.level);
    EXPECT_FALSE(rig.baro.isRunning());

    ASSERT_TRUE(rig.pass(BaroRecoveryPolicy::waitMs(1) - 1u));
    EXPECT_EQ(rig.pin.rises, rises);
    EXPECT_FALSE(rig.pin.level);

    ASSERT_TRUE(rig.pass(1));
    EXPECT_EQ(rig.pin.rises, rises + 1u);
}

// The same for a recovery: left 20 ms short of its next attempt with only
// the clock moved, the worker makes the attempt with no request to wake it.
TEST(BaroMs5837Recovery, WakesForTheNextAttemptByItself)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));

    rig.bus.ready = false;

    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    ASSERT_TRUE(rig.pass(BaroRecoveryPolicy::waitMs(1) - 20u));

    const uint32_t rises = rig.pin.risesSeen.load();

    Driver::System::AdvanceMs(20);

    EXPECT_TRUE(waitFor(rig.pin.risesSeen, rises + 1u));
    ASSERT_TRUE(settle());
}

// The wait is told as a difference modulo 2^32, so one that ends past the
// top of that count is still waited out. The millisecond clock does not get
// there: what happens where it does turn over is the case after this one.
TEST(BaroMs5837Recovery, AWaitPastTheTopOfTheCountIsWaitedOut)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());

    Driver::System::SetTimeMs(UINT32_MAX - 1000u);
    ASSERT_TRUE(rig.start(10.0f));

    rig.bus.ready = false;

    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    // The first attempt failed 700 ms short of the top; the second is due
    // past it.
    const uint32_t rises = rig.pin.rises;

    ASSERT_TRUE(rig.pass(BaroRecoveryPolicy::waitMs(1) - 1u));
    EXPECT_EQ(rig.pin.rises, rises);

    ASSERT_TRUE(rig.pass(1));
    EXPECT_EQ(rig.pin.rises, rises + 1u);
}

// The clock turns over short of 2^32, about every 48.5 days. A wait that ends
// past that point reads as long over once the clock is back at 0, so that
// one attempt is made early.
TEST(BaroMs5837Recovery, AWaitAcrossTheClockTurningOverEndsEarly)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());

    Driver::System::SetTimeMs(kClockTurnsAtMs - 1000u);
    ASSERT_TRUE(rig.start(10.0f));

    rig.bus.ready = false;

    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    // The first attempt failed 700 ms short of the turn; the second is due
    // at a count the clock never shows.
    const uint32_t rises = rig.pin.rises;

    ASSERT_TRUE(rig.pass(699));
    EXPECT_EQ(rig.pin.rises, rises);

    Driver::System::SetTimeMs(0);
    ASSERT_TRUE(rig.pass(0));

    EXPECT_EQ(rig.pin.rises, rises + 1u);
}

// A part that comes up and then fails its reads again: it is not left
// powered while it waits for the next attempt.
TEST(BaroMs5837Recovery, APartThatComesUpAndFailsAgainWaitsUnpowered)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));

    rig.bus.failAdc = true;

    // The first run of failures: power cycled at once, and up.
    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    ASSERT_TRUE(rig.pin.level);

    const uint32_t rises = rig.pin.rises;

    // The read made as it came up failed too; the run is completed here.
    for (uint32_t i = 1; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    EXPECT_FALSE(rig.pin.level);
    EXPECT_FALSE(rig.baro.isRunning());
    EXPECT_EQ(rig.pin.rises, rises);

    ASSERT_TRUE(rig.pass(BaroRecoveryPolicy::waitMs(1)));
    EXPECT_EQ(rig.pin.rises, rises + 1u);
    EXPECT_TRUE(rig.pin.level);
}

// A rate asked for while the part is being recovered is the one it measures
// at when it is back; the request does not hurry the recovery.
TEST(BaroMs5837Recovery, ARateChangeMidRecoveryIsAppliedOnceThePartIsBack)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(1.0f));
    ASSERT_EQ(rig.sink.pressures, 1u);

    rig.bus.ready = false;

    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(1000));
    }

    const uint32_t rises = rig.pin.rises;

    ASSERT_TRUE(rig.start(10.0f));
    EXPECT_EQ(rig.pin.rises, rises);

    rig.bus.ready = true;
    ASSERT_TRUE(rig.pass(BaroRecoveryPolicy::waitMs(1)));

    EXPECT_EQ(rig.pin.rises, rises + 1u);
    EXPECT_EQ(rig.sink.pressures, 2u);
    EXPECT_EQ(rig.sink.lastPeriodUs, 100000u);
}

// A part that never comes back: every attempt is made, each after its wait,
// and then the stream is given up and the next request starts afresh.
TEST(BaroMs5837Recovery, ADeadPartIsGivenUpAndTheNextRequestTriesAgain)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));

    const uint32_t risesAtStart = rig.pin.rises;

    rig.bus.ready = false;

    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    ASSERT_EQ(rig.pin.rises, risesAtStart + 1u);

    for (uint32_t attempt = 1; attempt < BaroRecoveryPolicy::skMaxAttempts;
         attempt++) {
        // A request while it waits does not hurry it.
        ASSERT_TRUE(rig.pass(0));
        ASSERT_EQ(rig.pin.rises, risesAtStart + attempt) << "attempt=" << attempt;

        ASSERT_TRUE(rig.pass(BaroRecoveryPolicy::waitMs(attempt)));
        ASSERT_EQ(rig.pin.rises, risesAtStart + attempt + 1u)
                << "attempt=" << attempt;
    }

    EXPECT_EQ(rig.pin.rises, risesAtStart + BaroRecoveryPolicy::skMaxAttempts);
    EXPECT_FALSE(rig.pin.level);
    EXPECT_FALSE(rig.baro.isRunning());
    EXPECT_TRUE(rig.baro.isInit());

    // Given up, not waiting: with no time passed, a request powers the part
    // at once, which a stream still being recovered would not do.
    rig.bus.ready = true;
    ASSERT_TRUE(rig.pass(0));

    EXPECT_EQ(rig.pin.rises,
              risesAtStart + BaroRecoveryPolicy::skMaxAttempts + 1u);
    EXPECT_TRUE(rig.baro.isRunning());
    EXPECT_EQ(rig.sink.pressures, 2u);
}

// A part that comes up after every power cycle and answers with the words it
// had before it: it never shows that it measures, so the attempts spent on it
// are not cleared and it is given up like one that does not answer.
TEST(BaroMs5837Recovery, APartFrozenOnOneValueIsGivenUp)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));

    const uint32_t risesAtStart = rig.pin.rises;
    const uint32_t risesAtEnd   = risesAtStart + BaroRecoveryPolicy::skMaxAttempts;

    rig.freeze();

    // Every wait, and after each the run of repeats that ends the attempt.
    uint32_t budgetMs = 0;

    for (uint32_t attempt = 0; attempt <= BaroRecoveryPolicy::skMaxAttempts;
         attempt++) {
        budgetMs += BaroRecoveryPolicy::waitMs(attempt)
                  + ((BaroRecoveryPolicy::skMaxRepeatedReads + 2u) * 100u);
    }

    // Stopped as soon as the last attempt is spent: a request after that
    // would start a stream of its own.
    for (uint32_t passedMs = 0;
         (passedMs < budgetMs) && ((rig.pin.rises < risesAtEnd) || rig.pin.level);
         passedMs += 100u) {
        ASSERT_TRUE(rig.pass(100));
    }

    EXPECT_EQ(rig.pin.rises, risesAtEnd);
    EXPECT_FALSE(rig.pin.level);
    EXPECT_FALSE(rig.baro.isRunning());

    // Given up, not waiting: with no time passed, a request powers the part
    // at once.
    ASSERT_TRUE(rig.pass(0));

    EXPECT_EQ(rig.pin.rises, risesAtEnd + 1u);
}

// The attempts belong to one stream. A stream asked for after one was given
// up gets its own, even if the part is still dead when it starts.
TEST(BaroMs5837Recovery, AStreamStartedAfterGivingUpGetsItsOwnAttempts)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));

    rig.bus.ready = false;

    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    for (uint32_t attempt = 1; attempt < BaroRecoveryPolicy::skMaxAttempts;
         attempt++) {
        ASSERT_TRUE(rig.pass(BaroRecoveryPolicy::waitMs(attempt)));
    }

    const uint32_t risesGivenUp = rig.pin.rises;

    // The start itself, and the first attempt, made at once.
    ASSERT_TRUE(rig.pass(0));
    EXPECT_EQ(rig.pin.rises, risesGivenUp + 2u);

    // And the second after its wait: the stream is being recovered.
    ASSERT_TRUE(rig.pass(BaroRecoveryPolicy::waitMs(1)));
    EXPECT_EQ(rig.pin.rises, risesGivenUp + 3u);
}

// A stop ends a recovery: nothing is attempted for a stream nobody wants.
TEST(BaroMs5837Recovery, AStopEndsARecovery)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));

    rig.bus.ready = false;

    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    ASSERT_TRUE(rig.stop());

    // A stream started after the stop is a new one: it powers the part at
    // once, where the old one would still have been waiting.
    const uint32_t rises = rig.pin.rises;

    const uint32_t pressures = rig.sink.pressures;

    rig.bus.ready = true;
    ASSERT_TRUE(rig.start(10.0f));

    EXPECT_EQ(rig.pin.rises, rises + 1u);
    EXPECT_TRUE(rig.baro.isRunning());

    // And measures at once: the old stream's wait is not this one's.
    EXPECT_EQ(rig.sink.pressures, pressures + 1u);
}

// The wait of a recovery that was stopped runs out with nobody wanting the
// stream: the part is not powered for it. The stop sent then asks for
// nothing and makes the worker look at the clock.
TEST(BaroMs5837Recovery, AStoppedRecoveryLeavesThePartOffWhenItsWaitRunsOut)
{
    Rig rig;
    ASSERT_TRUE(rig.bring());
    ASSERT_TRUE(rig.start(10.0f));

    rig.bus.ready = false;

    for (uint32_t i = 0; i < BaroRecoveryPolicy::skMaxFailedReads; i++) {
        ASSERT_TRUE(rig.pass(100));
    }

    // The first attempt was made at once and failed; the second is waiting.
    ASSERT_TRUE(rig.stop());

    const uint32_t rises = rig.pin.rises;
    ASSERT_FALSE(rig.pin.level);

    // A part that would come up, were it powered.
    rig.bus.ready = true;

    Driver::System::AdvanceMs(BaroRecoveryPolicy::waitMs(1) + 100u);
    ASSERT_TRUE(rig.stop());

    EXPECT_EQ(rig.pin.rises, rises);
    EXPECT_FALSE(rig.pin.level);
    EXPECT_FALSE(rig.baro.isRunning());
}
