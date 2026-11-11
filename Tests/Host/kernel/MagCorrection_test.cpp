/**
 ******************************************************************************
 * @file    MagCorrection_test.cpp
 * @brief   Host tests that the correction in force and the one kept in the
 *          store agree, and that a store refusing a write is reported. Driven
 *          through an in-memory fake store.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include "MagCorrection.hpp"

namespace {

/// A fit that MagCalibration will take.
MagCalibration::Record goodRecord(float offsetX = 19.2f)
{
    MagCalibration::Record record;

    record.offsetX  = offsetX;
    record.offsetY  = 90.7f;
    record.offsetZ  = -12.4f;
    record.m00      = 1.02f;
    record.m01      = 0.01f;
    record.m02      = -0.01f;
    record.m11      = 0.98f;
    record.m12      = 0.02f;
    record.m22      = 1.01f;
    record.radius   = 48.9f;
    record.residual = 0.9f;
    record.samples  = 750;

    return record;
}

/// A store that keeps a record in memory and can be told to refuse.
class FakeStore : public Interface::IMagCalibrationStore
{

public:

    bool save(const MagCalibration::Record& record) override
    {
        saves++;

        if (refuseSaves) {
            return false;
        }

        mHeld    = record;
        mHasHeld = true;

        return true;
    }

    bool load(MagCalibration::Record& record) const override
    {
        loads++;

        if (!mHasHeld) {
            return false;
        }

        record = mHeld;

        return true;
    }

    bool clear() override
    {
        clears++;

        if (refuseClears) {
            return false;
        }

        mHasHeld = false;

        return true;
    }

    /// What the store is holding, as a test can see it.
    bool held(MagCalibration::Record& record) const
    {
        record = mHeld;

        return mHasHeld;
    }

    void plant(const MagCalibration::Record& record)
    {
        mHeld    = record;
        mHasHeld = true;
    }

    bool refuseSaves  = false;
    bool refuseClears = false;

    uint32_t         saves  = 0;
    mutable uint32_t loads  = 0;
    uint32_t         clears = 0;

private:

    MagCalibration::Record mHeld {};
    bool                   mHasHeld = false;

};

} // namespace

// Coming up is when the kept correction is taken into force.
TEST(MagCorrection, ComesUpApplyingWhatWasKept)
{
    FakeStore store;

    store.plant(goodRecord());

    MagCorrection correction(store);

    EXPECT_EQ(store.loads, 1u);
    EXPECT_TRUE(correction.isCalibrated());

    MagCalibration::Record read;
    ASSERT_TRUE(correction.getCalibration(read));
    EXPECT_FLOAT_EQ(read.offsetX, 19.2f);
}

// The ordinary state of a watch that has never been calibrated.
TEST(MagCorrection, AnEmptyStoreIsNotAFailure)
{
    FakeStore     store;
    MagCorrection correction(store);

    EXPECT_FALSE(correction.isCalibrated());

    MagCalibration::Record read;
    EXPECT_FALSE(correction.getCalibration(read));

    // The sample is untouched, and the answer says it was not corrected.
    float x = 10.0f;
    float y = -20.0f;
    float z = 30.0f;

    EXPECT_FALSE(correction.apply(x, y, z));

    EXPECT_FLOAT_EQ(x, 10.0f);
    EXPECT_FLOAT_EQ(y, -20.0f);
    EXPECT_FLOAT_EQ(z, 30.0f);
}

TEST(MagCorrection, WhatIsInForceIsWhatWasKept)
{
    FakeStore     store;
    MagCorrection correction(store);
    ASSERT_TRUE(correction.setCalibration(goodRecord(33.3f)));

    MagCalibration::Record inForce;
    ASSERT_TRUE(correction.getCalibration(inForce));

    MagCalibration::Record kept;
    ASSERT_TRUE(store.held(kept));

    EXPECT_FLOAT_EQ(inForce.offsetX, 33.3f);
    EXPECT_FLOAT_EQ(kept.offsetX, 33.3f);
}

// A record the correction will not take must never reach the store, or a
// restart comes up believing in a correction that was never in force.
TEST(MagCorrection, ARecordThatCannotBeAppliedIsNeverKept)
{
    FakeStore     store;
    MagCorrection correction(store);

    MagCalibration::Record broken = goodRecord();
    broken.radius = 0.0f;

    EXPECT_FALSE(correction.setCalibration(broken));
    EXPECT_EQ(store.saves, 0u) << "a record that will not apply reached the store";
    EXPECT_FALSE(correction.isCalibrated());
}

// Applied now, but reported, because a restart will come up without it.
TEST(MagCorrection, AStoreThatRefusesIsReportedButStillApplied)
{
    FakeStore     store;
    MagCorrection correction(store);

    store.refuseSaves = true;

    EXPECT_FALSE(correction.setCalibration(goodRecord(44.4f)));

    EXPECT_TRUE(correction.isCalibrated())
            << "the correction was dropped because the store would not take it";

    MagCalibration::Record inForce;
    ASSERT_TRUE(correction.getCalibration(inForce));
    EXPECT_FLOAT_EQ(inForce.offsetX, 44.4f);
}

// Otherwise the next restart brings back the correction just erased.
TEST(MagCorrection, ClearingForgetsItInBothPlaces)
{
    FakeStore     store;
    MagCorrection correction(store);
    ASSERT_TRUE(correction.setCalibration(goodRecord()));

    EXPECT_TRUE(correction.clearCalibration());

    EXPECT_FALSE(correction.isCalibrated());
    EXPECT_EQ(store.clears, 1u);

    MagCalibration::Record kept;
    EXPECT_FALSE(store.held(kept));
}

// The correction stops now, but a restart would bring it back: reported.
TEST(MagCorrection, AStoreThatCannotEraseIsReported)
{
    FakeStore     store;
    MagCorrection correction(store);
    ASSERT_TRUE(correction.setCalibration(goodRecord()));

    store.refuseClears = true;

    EXPECT_FALSE(correction.clearCalibration());
    EXPECT_FALSE(correction.isCalibrated());
}

TEST(MagCorrection, ReplacingOneLeavesNothingOfTheOld)
{
    FakeStore     store;
    MagCorrection correction(store);
    ASSERT_TRUE(correction.setCalibration(goodRecord(11.1f)));
    ASSERT_TRUE(correction.setCalibration(goodRecord(22.2f)));

    MagCalibration::Record inForce;
    ASSERT_TRUE(correction.getCalibration(inForce));
    EXPECT_FLOAT_EQ(inForce.offsetX, 22.2f);

    MagCalibration::Record kept;
    ASSERT_TRUE(store.held(kept));
    EXPECT_FLOAT_EQ(kept.offsetX, 22.2f);
}

// Bytes that pass the store's integrity check but cannot be applied; the store
// cannot tell, so this is where it is caught.
TEST(MagCorrection, AKeptRecordThatCannotBeAppliedIsRefusedOnTheWayIn)
{
    FakeStore store;

    MagCalibration::Record broken = goodRecord();
    broken.radius = 0.0f;

    store.plant(broken);

    MagCorrection correction(store);

    EXPECT_FALSE(correction.isCalibrated());
}

// Without a lock it does nothing rather than everything unserialised: an inert
// correction reports an uncalibrated field, which callers already handle. It
// must not even read the store.
TEST(MagCorrection, WithoutItsLockItDoesNothingAtAll)
{
    FakeStore store;

    store.plant(goodRecord());

    const OS::MutexInitFailure noLocks;

    MagCorrection correction(store);

    EXPECT_EQ(store.loads, 0u) << "it read a store it cannot guard";
    EXPECT_FALSE(correction.isCalibrated());

    float x = 10.0f;
    float y = -20.0f;
    float z = 30.0f;

    EXPECT_FALSE(correction.apply(x, y, z));

    EXPECT_FLOAT_EQ(x, 10.0f);
    EXPECT_FLOAT_EQ(y, -20.0f);
    EXPECT_FLOAT_EQ(z, 30.0f);

    EXPECT_FALSE(correction.setCalibration(goodRecord()));
    EXPECT_EQ(store.saves, 0u) << "it wrote a record it cannot apply";

    EXPECT_FALSE(correction.clearCalibration());
    EXPECT_EQ(store.clears, 0u) << "it erased a store it does not use";

    MagCalibration::Record read;
    EXPECT_FALSE(correction.getCalibration(read));
}

// Correcting and saying so are one look at one record; two calls would let a
// calibration land between them and mislabel the sample.
TEST(MagCorrection, ApplyingAnswersAboutTheRecordItApplied)
{
    FakeStore     store;
    MagCorrection correction(store);

    float x = 19.2f;
    float y = 90.7f;
    float z = -12.4f;

    EXPECT_FALSE(correction.apply(x, y, z))
            << "an empty correction claimed it had corrected the sample";

    ASSERT_TRUE(correction.setCalibration(goodRecord()));

    // The record's own offsets land the sample at the centre of the sphere.
    x = 19.2f;
    y = 90.7f;
    z = -12.4f;

    EXPECT_TRUE(correction.apply(x, y, z));

    EXPECT_NEAR(x, 0.0f, 1e-4f);
    EXPECT_NEAR(y, 0.0f, 1e-4f);
    EXPECT_NEAR(z, 0.0f, 1e-4f);
}
