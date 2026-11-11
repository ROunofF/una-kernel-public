/**
 ******************************************************************************
 * @file    Bmi270FifoFrames_test.cpp
 * @brief   Host tests for the walk over the frames of a BMI270 FIFO read
 ******************************************************************************
 */

/**
 * Host unit tests for the walk over the frames of a BMI270 FIFO read in
 * header mode, and for the length a read asks the part for. The frame
 * layouts are the data sheet's; the reads are made up to them.
 */

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "Hardware/BMI270/FifoFrames.hpp"

namespace {

using namespace Hardware::Bmi270Fifo;

using Bytes = std::vector<uint8_t>;

constexpr uint8_t kAccel = 0x84;
constexpr uint8_t kGyro  = 0x88;
constexpr uint8_t kBoth  = 0x8C;

/// A reading frame whose payload bytes count up from 'first'.
Bytes reading(uint8_t header, uint8_t first)
{
    const size_t size = (header == kBoth) ? 12 : 6;

    Bytes frame { header };
    for (size_t i = 0; i < size; ++i) {
        frame.push_back(static_cast<uint8_t>(first + i));
    }

    return frame;
}

Bytes sensorTimeFrame(uint32_t counter)
{
    return { 0x44, static_cast<uint8_t>(counter), static_cast<uint8_t>(counter >> 8),
             static_cast<uint8_t>(counter >> 16) };
}

Bytes operator+(Bytes left, const Bytes& right)
{
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

FrameReader readerOf(const Bytes& read, uint16_t offset = 0)
{
    return FrameReader(read.data(), static_cast<uint16_t>(read.size()), offset);
}

TEST(Bmi270FifoFrames, ReadingsComeInTheOrderStored)
{
    const Bytes read = reading(kAccel, 0x10) + reading(kGyro, 0x20) + reading(kBoth, 0x30);

    FrameReader reader = readerOf(read);
    Frame       frame;

    ASSERT_TRUE(reader.next(frame));
    EXPECT_EQ(frame.type, FrameType::Accel);
    EXPECT_EQ(frame.payload, read.data() + 1);
    EXPECT_EQ(frame.payload[0], 0x10);

    ASSERT_TRUE(reader.next(frame));
    EXPECT_EQ(frame.type, FrameType::Gyro);
    EXPECT_EQ(frame.payload[0], 0x20);

    ASSERT_TRUE(reader.next(frame));
    EXPECT_EQ(frame.type, FrameType::GyroAccel);
    EXPECT_EQ(frame.payload[0], 0x30);
    EXPECT_EQ(frame.payload[11], 0x3B);

    EXPECT_FALSE(reader.next(frame));
    EXPECT_EQ(reader.end(), ReadEnd::Data);
    EXPECT_EQ(reader.offset(), read.size());
}

TEST(Bmi270FifoFrames, AnEmptyReadEndsAtOnce)
{
    const Bytes read;

    FrameReader reader = readerOf(read);
    Frame       frame;

    EXPECT_FALSE(reader.next(frame));
    EXPECT_EQ(reader.end(), ReadEnd::Data);
    EXPECT_EQ(reader.offset(), 0u);
}

// The read that empties the FIFO: the part's counter, then filler to the end.
TEST(Bmi270FifoFrames, SensorTimeFrameThenFiller)
{
    const Bytes read = reading(kAccel, 0) + sensorTimeFrame(0xABCDEF) + Bytes { 0x80, 0x80, 0x80 };

    FrameReader reader = readerOf(read);
    Frame       frame;

    ASSERT_TRUE(reader.next(frame));
    ASSERT_TRUE(reader.next(frame));
    EXPECT_EQ(frame.type, FrameType::SensorTime);
    EXPECT_EQ(sensorTime(frame), 0xABCDEFu);

    EXPECT_FALSE(reader.next(frame));
    EXPECT_EQ(reader.end(), ReadEnd::Filler);
    EXPECT_EQ(reader.offset(), 7u + 4u);
}

TEST(Bmi270FifoFrames, SkipFrameSaysHowManyWereDropped)
{
    const Bytes read = Bytes { 0x40, 3 } + reading(kAccel, 0);

    FrameReader reader = readerOf(read);
    Frame       frame;

    ASSERT_TRUE(reader.next(frame));
    EXPECT_EQ(frame.type, FrameType::Skip);
    EXPECT_EQ(skippedFrames(frame), 3);

    ASSERT_TRUE(reader.next(frame));
    EXPECT_EQ(frame.type, FrameType::Accel);
}

TEST(Bmi270FifoFrames, InputConfigFrameSaysWhatChangedAndWhen)
{
    const Bytes read = Bytes { 0x48, kAccelConfChanged | kGyroConfChanged, 0x00, 0x00, 0x5B } + reading(kAccel, 0);

    FrameReader reader = readerOf(read);
    Frame       frame;

    ASSERT_TRUE(reader.next(frame));
    EXPECT_EQ(frame.type, FrameType::InputConfig);
    EXPECT_EQ(configChanges(frame), 0x05);
    EXPECT_EQ(configSensorTime(frame), 0x5B0000u);

    ASSERT_TRUE(reader.next(frame));
    EXPECT_EQ(frame.type, FrameType::Accel);
}

// A read that stops inside a frame: the frame is not given, and the walk
// stands at its header, where the next read will put it whole.
TEST(Bmi270FifoFrames, AFrameCutShortIsNotTaken)
{
    const Bytes whole = reading(kAccel, 0) + reading(kBoth, 0x40);

    for (size_t cut = 8; cut < whole.size(); ++cut) {
        const Bytes read(whole.begin(), whole.begin() + cut);

        FrameReader reader = readerOf(read);
        Frame       frame;

        ASSERT_TRUE(reader.next(frame)) << cut;
        EXPECT_FALSE(reader.next(frame)) << cut;
        EXPECT_EQ(reader.end(), ReadEnd::Partial) << cut;
        EXPECT_EQ(reader.offset(), 7u) << cut;
    }
}

TEST(Bmi270FifoFrames, ControlFramesCutShortAreNotTaken)
{
    const Bytes read = reading(kGyro, 0) + Bytes { 0x44, 0x01, 0x02 };

    FrameReader reader = readerOf(read);
    Frame       frame;

    ASSERT_TRUE(reader.next(frame));
    EXPECT_FALSE(reader.next(frame));
    EXPECT_EQ(reader.end(), ReadEnd::Partial);
    EXPECT_EQ(reader.offset(), 7u);
}

// The next read is put where the walk stopped and the walk goes on from
// there: the frame cut short is there again, from its header.
TEST(Bmi270FifoFrames, TheWalkGoesOnOverTheNextRead)
{
    const Bytes first  = reading(kAccel, 0) + Bytes { kAccel, 0x10, 0x11 };
    const Bytes second = reading(kAccel, 0x10) + sensorTimeFrame(1234);

    FrameReader cut = readerOf(first);
    Frame       frame;

    while (cut.next(frame)) {
    }
    ASSERT_EQ(cut.end(), ReadEnd::Partial);

    Bytes both(first.begin(), first.begin() + cut.offset());
    both = both + second;

    FrameReader reader = readerOf(both, cut.offset());

    ASSERT_TRUE(reader.next(frame));
    EXPECT_EQ(frame.type, FrameType::Accel);
    EXPECT_EQ(frame.payload[0], 0x10);

    ASSERT_TRUE(reader.next(frame));
    EXPECT_EQ(frame.type, FrameType::SensorTime);
    EXPECT_EQ(sensorTime(frame), 1234u);
    EXPECT_EQ(reader.offset(), both.size());
}

// Auxiliary data is not read by this driver; its frames have no length known
// here, so nothing after one can be trusted.
TEST(Bmi270FifoFrames, AnUnknownHeaderStopsTheWalk)
{
    const Bytes read = reading(kAccel, 0) + Bytes { 0x90, 1, 2, 3 };

    FrameReader reader = readerOf(read);
    Frame       frame;

    ASSERT_TRUE(reader.next(frame));
    EXPECT_FALSE(reader.next(frame));
    EXPECT_EQ(reader.end(), ReadEnd::Unknown);
    EXPECT_EQ(reader.offset(), 7u);
}

// The two low bits of a header tag the frame with the interrupt pins' state.
TEST(Bmi270FifoFrames, InterruptTagsAreNotPartOfTheFrameType)
{
    Bytes read = reading(kGyro, 0);
    read[0] |= 0x03;

    FrameReader reader = readerOf(read);
    Frame       frame;

    ASSERT_TRUE(reader.next(frame));
    EXPECT_EQ(frame.type, FrameType::Gyro);
}

// ---------------------------------------------------------------------------
// Hardware::Bmi270Fifo::readLength
// ---------------------------------------------------------------------------

TEST(Bmi270FifoFrames, AReadAsksForMoreThanTheFifoHeld)
{
    // Room for the sensortime frame and a frame that came in the meantime.
    EXPECT_EQ(readLength(0, 3072), 16u);
    EXPECT_EQ(readLength(7, 3072), 7u + 2u + 16u);

    // And for what comes while a long read runs: three tenths more.
    EXPECT_EQ(readLength(1000, 3072), 1000u + 300u + 16u);
    EXPECT_EQ(readLength(2016, 3072), 2016u + 604u + 16u);
}

TEST(Bmi270FifoFrames, AReadIsHeldToTheRoomThereIs)
{
    EXPECT_EQ(readLength(2016, 2048), 2048u);
    EXPECT_EQ(readLength(100, 10), 10u);
    EXPECT_EQ(readLength(0, 0), 0u);
}

} // namespace
