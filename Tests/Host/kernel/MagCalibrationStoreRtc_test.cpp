/**
 * Host unit tests for MagCalibrationStoreRtc - the calibration kept in the RTC
 * backup domain. The fake is that domain: registers that outlive the store
 * object, as the real ones outlive a reset.
 */

#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "MagCalibrationStoreRtc.hpp"

namespace {

constexpr uint8_t kFirstReg = 13;
constexpr uint8_t kCrcReg   = kFirstReg + MagCalibrationStoreRtc::skPayloadRegs;

/// Any value; the store does not interpret it.
constexpr uint32_t kFrameTag      = 0x4D414732U;
constexpr uint32_t kOtherFrameTag = 0x4D414733U;

class FakeRtc : public Interface::IRtc
{
public:
    Interface::IRtc::DateTime get() override { return {}; }
    bool set(const std::tm&) override { return true; }
    int  getTimeZone() override { return 0; }
    void setTimeZone(int) override {}
    int  getDstOffset() override { return 0; }
    void setDstOffset(int) override {}

    void writeBackupReg(uint32_t reg, uint32_t value) override
    {
        if (reg < kRegisters) {
            mRegisters[reg] = value;
        }
    }

    uint32_t readBackupReg(uint32_t reg) override
    {
        return (reg < kRegisters) ? mRegisters[reg] : 0;
    }

    static constexpr uint32_t kRegisters = 32;

    uint32_t mRegisters[kRegisters] = { 0 };
};

// Write protection still set: writes vanish without an error, which save()
// and clear() have to notice.
class ReadOnlyRtc : public FakeRtc
{
public:
    void writeBackupReg(uint32_t reg, uint32_t value) override
    {
        (void) reg;
        (void) value;
    }
};

MagCalibration::Record sampleRecord()
{
    MagCalibration::Record record;

    record.offsetX  = 19.204262f;
    record.offsetY  = 90.740242f;
    record.offsetZ  = -30.501537f;
    record.m00      = 1.02f;
    record.m01      = 0.03f;
    record.m02      = -0.01f;
    record.m11      = 0.97f;
    record.m12      = 0.02f;
    record.m22      = 1.03f;
    record.radius   = 44.308029f;
    record.residual = 3.257628f;
    record.samples  = 752;

    return record;
}

} // namespace

TEST(MagCalibrationStoreRtc, ReadsBackWhatItWrote)
{
    FakeRtc rtc;
    MagCalibrationStoreRtc store(rtc, kFirstReg, kFrameTag);

    const MagCalibration::Record written = sampleRecord();
    ASSERT_TRUE(store.save(written));

    MagCalibration::Record read;
    ASSERT_TRUE(store.load(read));

    EXPECT_FLOAT_EQ(read.offsetX, written.offsetX);
    EXPECT_FLOAT_EQ(read.offsetY, written.offsetY);
    EXPECT_FLOAT_EQ(read.offsetZ, written.offsetZ);
    EXPECT_FLOAT_EQ(read.m00,     written.m00);
    EXPECT_FLOAT_EQ(read.m01,     written.m01);
    EXPECT_FLOAT_EQ(read.m02,     written.m02);
    EXPECT_FLOAT_EQ(read.m11,     written.m11);
    EXPECT_FLOAT_EQ(read.m12,     written.m12);
    EXPECT_FLOAT_EQ(read.m22,     written.m22);
    EXPECT_FLOAT_EQ(read.radius,  written.radius);
}

TEST(MagCalibrationStoreRtc, ReportsAWriteThatDidNotLand)
{
    ReadOnlyRtc rtc;
    MagCalibrationStoreRtc store(rtc, kFirstReg, kFrameTag);

    EXPECT_FALSE(store.save(sampleRecord()));

    MagCalibration::Record read;
    EXPECT_FALSE(store.load(read));
}

// Residual and sample count are not stored; they must not come back invented.
TEST(MagCalibrationStoreRtc, DoesNotInventTheFitMetadata)
{
    FakeRtc rtc;
    MagCalibrationStoreRtc store(rtc, kFirstReg, kFrameTag);

    ASSERT_TRUE(store.save(sampleRecord()));

    MagCalibration::Record read;
    ASSERT_TRUE(store.load(read));

    EXPECT_FLOAT_EQ(read.residual, 0.0f);
    EXPECT_EQ(read.samples, 0u);
}

// A correction only means something in the axes it was fitted in, and a frame
// change leaves every stored byte intact: only the tag catches it.
TEST(MagCalibrationStoreRtc, RefusesARecordWrittenInAnotherFrame)
{
    FakeRtc rtc;

    const MagCalibration::Record written = sampleRecord();

    MagCalibrationStoreRtc store(rtc, kFirstReg, kFrameTag);
    ASSERT_TRUE(store.save(written));

    // Same registers, same bytes, same layout - only the frame differs.
    MagCalibrationStoreRtc other(rtc, kFirstReg, kOtherFrameTag);

    MagCalibration::Record read = sampleRecord();
    EXPECT_FALSE(other.load(read));

    // Still good to its writer, so the refusal above is the tag.
    EXPECT_TRUE(store.load(read));
}

// A blank domain, as after a battery disconnect, reads as not calibrated
// rather than as an all-zero calibration.
TEST(MagCalibrationStoreRtc, RejectsABlankDomain)
{
    FakeRtc rtc;
    MagCalibrationStoreRtc store(rtc, kFirstReg, kFrameTag);

    MagCalibration::Record read = sampleRecord();
    EXPECT_FALSE(store.load(read));
}

TEST(MagCalibrationStoreRtc, RejectsACorruptedPayload)
{
    FakeRtc rtc;
    MagCalibrationStoreRtc store(rtc, kFirstReg, kFrameTag);

    ASSERT_TRUE(store.save(sampleRecord()));

    for (uint8_t idx = 0; idx < MagCalibrationStoreRtc::skPayloadRegs; ++idx) {
        FakeRtc corrupted = rtc;
        corrupted.mRegisters[kFirstReg + idx] ^= 0x00000001u;

        MagCalibrationStoreRtc corruptedStore(corrupted, kFirstReg, kFrameTag);

        MagCalibration::Record read;
        EXPECT_FALSE(corruptedStore.load(read)) << "register " << int(idx);
    }
}

TEST(MagCalibrationStoreRtc, RejectsACorruptedCrc)
{
    FakeRtc rtc;
    MagCalibrationStoreRtc store(rtc, kFirstReg, kFrameTag);

    ASSERT_TRUE(store.save(sampleRecord()));
    rtc.mRegisters[kCrcReg] ^= 0x00000001u;

    MagCalibration::Record read;
    EXPECT_FALSE(store.load(read));
}

TEST(MagCalibrationStoreRtc, ClearMakesTheNextLoadFail)
{
    FakeRtc rtc;
    MagCalibrationStoreRtc store(rtc, kFirstReg, kFrameTag);

    ASSERT_TRUE(store.save(sampleRecord()));

    MagCalibration::Record read;
    ASSERT_TRUE(store.load(read));

    EXPECT_TRUE(store.clear());
    EXPECT_FALSE(store.load(read));
}

// A dropped erase would bring the calibration back at the next restart.
TEST(MagCalibrationStoreRtc, ReportsAnEraseThatDidNotLand)
{
    ReadOnlyRtc rtc;
    MagCalibrationStoreRtc store(rtc, kFirstReg, kFrameTag);

    // A kept calibration, planted past the write protection.
    rtc.mRegisters[kFirstReg] = 0x41200000U;
    rtc.mRegisters[kCrcReg]   = 0x12345678U;

    EXPECT_FALSE(store.clear());
}

// The domain is shared with the clock, timezone and GPS aiding reference.
TEST(MagCalibrationStoreRtc, TouchesOnlyItsOwnRegisters)
{
    FakeRtc rtc;

    for (uint32_t reg = 0; reg < FakeRtc::kRegisters; ++reg) {
        rtc.mRegisters[reg] = 0xA5A50000u | reg;
    }

    MagCalibrationStoreRtc store(rtc, kFirstReg, kFrameTag);
    ASSERT_TRUE(store.save(sampleRecord()));

    for (uint32_t reg = 0; reg < FakeRtc::kRegisters; ++reg) {
        const bool owned = (reg >= kFirstReg &&
                            reg < (kFirstReg + MagCalibrationStoreRtc::skPayloadRegs)) ||
                           (reg == kCrcReg);
        if (!owned) {
            EXPECT_EQ(rtc.mRegisters[reg], 0xA5A50000u | reg)
                    << "register " << reg << " was written";
        }
    }
}
