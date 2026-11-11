/**
 ******************************************************************************
 * @file    BMM350_test.cpp
 * @brief   Host tests for the BMM350 hardware driver: the ODR / averaging
 *          mappers, the datasheet averaging limits, and the bus protocol under
 *          the vendor SensorAPI - pad bytes, retry budget, device address, and
 *          the gate that keeps an uninitialised part off a shared bus.
 *
 *          Driven through a register-level fake bus. init() is covered only
 *          for the local bound on the OTP download.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>

#include "OS.hpp"

#include "BMM350/BMM350.hpp"

using Hardware::BMM350;

namespace {

// Every BMM350 read answers with two pad bytes before the register content.
// Written out rather than taken from the vendor header, so a change upstream
// fails a test instead of being followed silently.
constexpr uint16_t kDummyBytes = 2;

// -----------------------------------------------------------------------------
// Fake bus answering like a BMM350: a flat register file behind the two pad
// bytes. It counts transfers and can reject a number of reads first.
// -----------------------------------------------------------------------------
class FakeI2c : public Interface::II2c
{
public:
    uint8_t  regs[256]  = {};
    bool     ready      = true;

    uint32_t failReads  = 0;

    uint32_t readCalls  = 0;
    uint32_t writeCalls = 0;
    uint8_t  lastAddr   = 0xFF;
    uint16_t lastLen    = 0;
    uint16_t lastReg    = 0xFFFF;

    uint32_t transfers() const { return readCalls + writeCalls; }

    bool init() override      { return true; }
    void deinit() override    {}
    bool isBusBusy() override { return false; }
    void setCpltHandler(Interface::II2c::CpltHandler*) override {}

    bool isDeviceReady(uint8_t devAddr) override
    {
        lastAddr = devAddr;
        return ready;
    }

    bool transmit(uint8_t, const uint8_t*, uint16_t, bool = true) override
    {
        return false;
    }

    bool receive(uint8_t, uint8_t*, uint16_t, bool = true) override
    {
        return false;
    }

    bool writeMem(uint8_t devAddr, uint16_t memAddr, uint8_t,
                  const uint8_t* pData, uint16_t size, bool = true) override
    {
        lastAddr = devAddr;
        lastReg  = memAddr;
        lastLen  = size;
        writeCalls++;

        for (uint16_t i = 0; i < size; i++) {
            regs[(memAddr + i) & 0xFFu] = pData[i];
        }

        return true;
    }

    bool readMem(uint8_t devAddr, uint16_t memAddr, uint8_t, uint8_t* pData,
                 uint16_t size, bool = true) override
    {
        lastAddr = devAddr;
        lastReg  = memAddr;
        lastLen  = size;
        readCalls++;

        if (failReads > 0) {
            failReads--;
            return false;
        }

        for (uint16_t i = 0; i < size; i++) {
            pData[i] = (i < kDummyBytes)
                     ? 0xAAu
                     : regs[(memAddr + i - kDummyBytes) & 0xFFu];
        }

        return true;
    }
};

/// Datasheet periods of the nine rungs, typed out independently of the driver.
struct Rung {
    BMM350::Odr odr;
    uint32_t    periodUs;
};

const Rung kRungs[] = {
    { BMM350::Odr::ODR_400_HZ,      2500 },
    { BMM350::Odr::ODR_200_HZ,      5000 },
    { BMM350::Odr::ODR_100_HZ,     10000 },
    { BMM350::Odr::ODR_50_HZ,      20000 },
    { BMM350::Odr::ODR_25_HZ,      40000 },
    { BMM350::Odr::ODR_12_5_HZ,    80000 },
    { BMM350::Odr::ODR_6_25_HZ,   160000 },
    { BMM350::Odr::ODR_3_125_HZ,  320000 },
    { BMM350::Odr::ODR_1_5625_HZ, 640000 },
};

const BMM350::Averaging kAveragings[] = {
    BMM350::Averaging::AVG_1,
    BMM350::Averaging::AVG_2,
    BMM350::Averaging::AVG_4,
    BMM350::Averaging::AVG_8,
};

unsigned toNumber(BMM350::Odr odr)
{
    return static_cast<unsigned>(odr);
}

unsigned toNumber(BMM350::Averaging avg)
{
    return static_cast<unsigned>(avg);
}

} // namespace

// -----------------------------------------------------------------------------
// Rate mapping
// -----------------------------------------------------------------------------

TEST(Bmm350Odr, PeriodOfEveryRungMatchesTheDatasheet)
{
    for (const Rung& rung : kRungs) {
        EXPECT_EQ(BMM350::odrToPeriodUs(rung.odr), rung.periodUs)
                << "odr=" << toNumber(rung.odr);
    }
}

TEST(Bmm350Odr, AskingForARungsOwnPeriodReturnsThatRung)
{
    for (const Rung& rung : kRungs) {
        EXPECT_EQ(toNumber(BMM350::periodUsToOdr(rung.periodUs)),
                  toNumber(rung.odr))
                << "periodUs=" << rung.periodUs;
    }
}

// A caller never ends up slower than it asked for; timeouts and watchdogs
// scheduled against the applied period depend on it.
TEST(Bmm350Odr, ResultIsNeverSlowerThanTheRequest)
{
    for (uint32_t periodUs = 2500; periodUs <= 700000; periodUs += 137) {
        const uint32_t applied =
                BMM350::odrToPeriodUs(BMM350::periodUsToOdr(periodUs));
        EXPECT_LE(applied, periodUs) << "periodUs=" << periodUs;
    }
}

TEST(Bmm350Odr, JustUnderARungStepsToTheFasterNeighbour)
{
    for (size_t i = 1; i < std::size(kRungs); i++) {
        EXPECT_EQ(toNumber(BMM350::periodUsToOdr(kRungs[i].periodUs - 1)),
                  toNumber(kRungs[i - 1].odr))
                << "periodUs=" << kRungs[i].periodUs - 1;
    }
}

TEST(Bmm350Odr, RequestsBeyondEitherEndSaturate)
{
    EXPECT_EQ(toNumber(BMM350::periodUsToOdr(0)),
              toNumber(BMM350::Odr::ODR_400_HZ));
    EXPECT_EQ(toNumber(BMM350::periodUsToOdr(2499)),
              toNumber(BMM350::Odr::ODR_400_HZ));
    EXPECT_EQ(toNumber(BMM350::periodUsToOdr(640001)),
              toNumber(BMM350::Odr::ODR_1_5625_HZ));
    EXPECT_EQ(toNumber(BMM350::periodUsToOdr(UINT32_MAX)),
              toNumber(BMM350::Odr::ODR_1_5625_HZ));
}

// -----------------------------------------------------------------------------
// Averaging limits
// -----------------------------------------------------------------------------

TEST(Bmm350Averaging, FastRungsClampToTheirDatasheetCeiling)
{
    EXPECT_EQ(toNumber(BMM350::clampAveraging(BMM350::Odr::ODR_400_HZ,
                                              BMM350::Averaging::AVG_8)),
              toNumber(BMM350::Averaging::AVG_1));
    EXPECT_EQ(toNumber(BMM350::clampAveraging(BMM350::Odr::ODR_400_HZ,
                                              BMM350::Averaging::AVG_2)),
              toNumber(BMM350::Averaging::AVG_1));
    EXPECT_EQ(toNumber(BMM350::clampAveraging(BMM350::Odr::ODR_200_HZ,
                                              BMM350::Averaging::AVG_8)),
              toNumber(BMM350::Averaging::AVG_2));
    EXPECT_EQ(toNumber(BMM350::clampAveraging(BMM350::Odr::ODR_100_HZ,
                                              BMM350::Averaging::AVG_8)),
              toNumber(BMM350::Averaging::AVG_4));
}

TEST(Bmm350Averaging, ARequestUnderTheCeilingIsLeftAlone)
{
    EXPECT_EQ(toNumber(BMM350::clampAveraging(BMM350::Odr::ODR_400_HZ,
                                              BMM350::Averaging::AVG_1)),
              toNumber(BMM350::Averaging::AVG_1));
    EXPECT_EQ(toNumber(BMM350::clampAveraging(BMM350::Odr::ODR_200_HZ,
                                              BMM350::Averaging::AVG_1)),
              toNumber(BMM350::Averaging::AVG_1));
    EXPECT_EQ(toNumber(BMM350::clampAveraging(BMM350::Odr::ODR_100_HZ,
                                              BMM350::Averaging::AVG_2)),
              toNumber(BMM350::Averaging::AVG_2));
}

TEST(Bmm350Averaging, RungsAtFiftyHertzAndBelowTakeAnything)
{
    for (const Rung& rung : kRungs) {
        if (rung.periodUs < 20000) {
            continue;
        }
        for (BMM350::Averaging avg : kAveragings) {
            EXPECT_EQ(toNumber(BMM350::clampAveraging(rung.odr, avg)),
                      toNumber(avg))
                    << "odr=" << toNumber(rung.odr);
        }
    }
}

// Raising would program a pair the part cannot keep up with.
TEST(Bmm350Averaging, ClampingNeverRaisesTheRequest)
{
    for (const Rung& rung : kRungs) {
        for (BMM350::Averaging avg : kAveragings) {
            EXPECT_LE(toNumber(BMM350::clampAveraging(rung.odr, avg)),
                      toNumber(avg))
                    << "odr=" << toNumber(rung.odr);
        }
    }
}

// -----------------------------------------------------------------------------
// Bus protocol
// -----------------------------------------------------------------------------

TEST(Bmm350Bus, NothingWorksBeforeConfigure)
{
    BMM350  drv;
    uint8_t chipId = 0;

    EXPECT_FALSE(drv.isReady());
    EXPECT_FALSE(drv.readChipId(chipId));
    EXPECT_FALSE(drv.init());
    EXPECT_FALSE(drv.isInitialized());
}

TEST(Bmm350Bus, ProbeUsesTheStrappedAddress)
{
    FakeI2c bus;
    BMM350  drv;

    bus.regs[BMM350_REG_CHIP_ID] = BMM350::skChipId;
    drv.configure(bus);

    EXPECT_TRUE(drv.isReady());
    EXPECT_EQ(bus.lastAddr, BMM350::skI2cAddr);
}

TEST(Bmm350Bus, ProbeFailsWhenNobodyAcknowledges)
{
    FakeI2c bus;
    BMM350  drv;

    bus.regs[BMM350_REG_CHIP_ID] = BMM350::skChipId;
    bus.ready = false;
    drv.configure(bus);

    EXPECT_FALSE(drv.isReady());
    EXPECT_EQ(bus.readCalls, 0u)
            << "a part that did not acknowledge should not then be read";
}

TEST(Bmm350Bus, ProbeRejectsAForeignPart)
{
    FakeI2c bus;
    BMM350  drv;

    bus.regs[BMM350_REG_CHIP_ID] = 0x24;    // some other part at the address
    drv.configure(bus);

    EXPECT_FALSE(drv.isReady());
}

// Getting the pad bytes wrong shifts every register read by two: a part that
// looks alive and talks nonsense.
TEST(Bmm350Bus, ARegisterReadSkipsTheTwoPadBytes)
{
    FakeI2c bus;
    BMM350  drv;
    uint8_t chipId = 0;

    bus.regs[BMM350_REG_CHIP_ID] = BMM350::skChipId;
    drv.configure(bus);

    ASSERT_TRUE(drv.readChipId(chipId));
    EXPECT_EQ(chipId, BMM350::skChipId);
    EXPECT_EQ(bus.lastReg, static_cast<uint16_t>(BMM350_REG_CHIP_ID));
    EXPECT_EQ(bus.lastLen, static_cast<uint16_t>(1 + kDummyBytes))
            << "the pad bytes have to be asked for, not assumed";
}

TEST(Bmm350Bus, ATransientFailureIsRetried)
{
    FakeI2c bus;
    BMM350  drv;
    uint8_t chipId = 0;

    bus.regs[BMM350_REG_CHIP_ID] = BMM350::skChipId;
    drv.configure(bus);

    // A lone refusal on a bus the driver does not own is traffic, not a fault.
    bus.failReads = 1;
    EXPECT_TRUE(drv.readChipId(chipId));
    EXPECT_EQ(chipId, BMM350::skChipId);
    EXPECT_EQ(bus.readCalls, 2u);
}

TEST(Bmm350Bus, ASuccessfulTransferWaitsForNothing)
{
    FakeI2c bus;
    BMM350  drv;
    uint8_t chipId = 0;

    bus.regs[BMM350_REG_CHIP_ID] = BMM350::skChipId;
    drv.configure(bus);

    OS::gDelayedMs = 0;
    ASSERT_TRUE(drv.readChipId(chipId));
    EXPECT_EQ(OS::gDelayedMs, 0u) << "the happy path must not pace itself";
}

// A NAK comes straight back, so without a pause the budget burns in
// microseconds. The pause belongs between attempts, never after the last.
TEST(Bmm350Bus, AttemptsArePacedAndTheLastIsNotFollowedByAPause)
{
    FakeI2c bus;
    BMM350  drv;
    uint8_t chipId = 0;

    bus.regs[BMM350_REG_CHIP_ID] = BMM350::skChipId;
    drv.configure(bus);

    // One refusal, then success: exactly one gap.
    bus.failReads  = 1;
    OS::gDelayedMs = 0;
    ASSERT_TRUE(drv.readChipId(chipId));

    const uint32_t oneGapMs = OS::gDelayedMs;
    EXPECT_GT(oneGapMs, 0u) << "a retry has to give the part time";

    bus.failReads  = 1000;
    bus.readCalls  = 0;
    OS::gDelayedMs = 0;
    ASSERT_FALSE(drv.readChipId(chipId));

    ASSERT_GT(bus.readCalls, 1u);
    EXPECT_EQ(OS::gDelayedMs, oneGapMs * (bus.readCalls - 1));
}

TEST(Bmm350Bus, TheRetryBudgetIsFiniteAndFullySpent)
{
    FakeI2c bus;
    BMM350  drv;
    uint8_t chipId = 0;

    bus.regs[BMM350_REG_CHIP_ID] = BMM350::skChipId;
    drv.configure(bus);

    // Far more failures than any budget covers: give up, but not at the first.
    bus.failReads = 1000;
    EXPECT_FALSE(drv.readChipId(chipId));

    const uint32_t attempts = bus.readCalls;
    EXPECT_GT(attempts, 1u);
    EXPECT_LT(attempts, 16u) << "a wedged part must not hold the shared bus";

    // The last attempt of the budget still counts as an attempt.
    bus.failReads = attempts - 1;
    bus.readCalls = 0;
    EXPECT_TRUE(drv.readChipId(chipId));
    EXPECT_EQ(bus.readCalls, attempts);
}

// Only the probe may reach the shared bus before init() succeeds; a stray
// transfer fails somebody else's.
TEST(Bmm350Bus, AnUninitialisedPartStaysOffTheBus)
{
    FakeI2c        bus;
    BMM350         drv;
    BMM350::Sample sample {};
    bool           ready = false;

    drv.configure(bus);
    ASSERT_FALSE(drv.isInitialized());

    EXPECT_FALSE(drv.setOdr(BMM350::Odr::ODR_25_HZ, BMM350::Averaging::AVG_4));
    EXPECT_FALSE(drv.setPowerMode(BMM350::PowerMode::NORMAL));
    EXPECT_FALSE(drv.configureInterrupt(BMM350::InterruptConfig {}));
    EXPECT_FALSE(drv.enableInterrupt(true));
    EXPECT_FALSE(drv.getDataReady(ready));
    EXPECT_FALSE(drv.readSample(sample));
    EXPECT_FALSE(drv.measureForced(sample));
    EXPECT_FALSE(drv.setI2cWatchdog(true, false));

    EXPECT_EQ(bus.transfers(), 0u);
}

// -----------------------------------------------------------------------------
// The OTP download the SensorAPI runs inside init(), and its local bound.
// -----------------------------------------------------------------------------

// OTP status reset value: command-done clear, no error. Also exactly what a
// part whose OTP controller never runs the command keeps answering.
constexpr uint8_t kOtpStatusReset = 0x10;

// The same register once the command has completed: done set, still no error.
constexpr uint8_t kOtpStatusDone  = 0x11;

// A part can answer every read and never finish an OTP command; the vendor
// poll would spin forever, so it is bounded locally. Without the bound this
// test hangs rather than fails: init() has to come back.
TEST(Bmm350Otp, AStalledOtpCommandDoesNotTrapInit)
{
    FakeI2c bus;
    BMM350  drv;

    bus.regs[BMM350_REG_CHIP_ID]        = BMM350::skChipId;
    bus.regs[BMM350_REG_OTP_STATUS_REG] = kOtpStatusReset;

    drv.configure(bus);

    EXPECT_FALSE(drv.init());
    EXPECT_FALSE(drv.isInitialized())
            << "a part that never finished its OTP download is not initialised";

    // Only that a ceiling exists is pinned, low enough to catch a bound that
    // is a hang in all but name.
    EXPECT_LT(bus.readCalls, 1000u);
}

// The bound must be inert on a part that behaves: a completing command is
// seen by the first poll of each word.
TEST(Bmm350Otp, ACompletingOtpCommandDoesNotPayForTheBound)
{
    FakeI2c stalled;
    FakeI2c completing;
    BMM350  drvStalled;
    BMM350  drvCompleting;

    stalled.regs[BMM350_REG_CHIP_ID]           = BMM350::skChipId;
    stalled.regs[BMM350_REG_OTP_STATUS_REG]    = kOtpStatusReset;
    completing.regs[BMM350_REG_CHIP_ID]        = BMM350::skChipId;
    completing.regs[BMM350_REG_OTP_STATUS_REG] = kOtpStatusDone;

    drvStalled.configure(stalled);
    drvCompleting.configure(completing);

    (void) drvStalled.init();
    (void) drvCompleting.init();

    EXPECT_LT(completing.readCalls, stalled.readCalls / 4)
            << "a part that answers should not be paying the stalled part's "
               "poll budget";
}
