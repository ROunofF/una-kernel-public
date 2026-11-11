/**
 * @file EpoParser_test.cpp
 * @brief What an EPO injection sends, read through a small buffer.
 *
 * The parser used to need the whole file in memory - 64 KB for GPS - and the
 * injection then sent one 6-hour block of it. It now reads through a scratch
 * buffer, so what matters is that it detects the same file types and times,
 * and selects the same segments, whatever the size of that buffer, without
 * depending on the blocks being in order, and that a read failure is reported
 * rather than turned into ephemeris. The files here are synthetic: only the
 * fields the parser decodes are meaningful.
 */

#include <gtest/gtest.h>

#include "AG3335/AirohaEpoParser.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

using Airoha::EpoBuffer;
using Airoha::EpoFileType;
using Airoha::EpoParser;
using Airoha::MemoryEpoReader;
using Airoha::SystemType;

namespace {

constexpr size_t kSeg = EpoParser::kSegmentSize;
constexpr uint32_t kHoursPerWeek = 168;

/// Week 2300, hour 0: a whole number of 6-hour blocks from the epoch.
constexpr uint32_t kStartHour = 2300 * kHoursPerWeek;

uint32_t weekOf(uint32_t hour) { return hour / kHoursPerWeek; }
uint32_t towOf(uint32_t hour) { return (hour % kHoursPerWeek) * 3600; }

/// One segment: the GPS hour and SV ID where the parser reads them, and a
/// body derived from both so that two segments never compare equal by chance.
void appendSegment(std::vector<uint8_t> &file, uint32_t hour, uint8_t sv)
{
    const size_t at = file.size();
    file.resize(at + kSeg);
    file[at + 0] = static_cast<uint8_t>(hour);
    file[at + 1] = static_cast<uint8_t>(hour >> 8);
    file[at + 2] = static_cast<uint8_t>(hour >> 16);
    file[at + 3] = sv;
    for (size_t i = 4; i < kSeg; ++i) {
        file[at + i] = static_cast<uint8_t>(sv * 31 + hour + i);
    }
}

std::vector<uint8_t> svRange(uint8_t first, uint8_t count)
{
    std::vector<uint8_t> svs(count);
    for (uint8_t i = 0; i < count; ++i) {
        svs[i] = static_cast<uint8_t>(first + i);
    }
    return svs;
}

/// A fixed-format file: @p blocks 6-hour blocks of every SV in @p svs.
std::vector<uint8_t> fixedFile(const std::vector<uint8_t> &svs, size_t blocks)
{
    std::vector<uint8_t> file;
    for (size_t b = 0; b < blocks; ++b) {
        for (uint8_t sv : svs) {
            appendSegment(file, kStartHour + static_cast<uint32_t>(6 * b), sv);
        }
    }
    return file;
}

/// A variable-format file: a header carrying @p headerSv (254 Galileo, 255
/// BeiDou) and a satellite mask, then the blocks.
std::vector<uint8_t> variableFile(uint8_t headerSv, const std::vector<uint8_t> &svs, size_t blocks)
{
    std::vector<uint8_t> file(kSeg, 0);
    file[3] = headerSv;
    const uint32_t mask = (svs.size() >= 32) ? 0xFFFFFFFFu
                                             : ((1u << svs.size()) - 1u);
    file[4] = static_cast<uint8_t>(mask);
    file[5] = static_cast<uint8_t>(mask >> 8);
    file[6] = static_cast<uint8_t>(mask >> 16);
    file[7] = static_cast<uint8_t>(mask >> 24);
    for (size_t i = 8; i < kSeg; ++i) {
        file[i] = static_cast<uint8_t>(0xA0 + i);
    }

    const std::vector<uint8_t> body = fixedFile(svs, blocks);
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

/// Byte 3 of every segment in a selection.
std::vector<uint8_t> svsOf(const EpoBuffer &segments)
{
    std::vector<uint8_t> svs;
    for (size_t at = 0; at + kSeg <= segments.size(); at += kSeg) {
        svs.push_back(segments.data()[at + 3]);
    }
    return svs;
}

/// GPS hour of every segment in a selection.
std::vector<uint32_t> hoursOf(const EpoBuffer &segments)
{
    std::vector<uint32_t> hours;
    for (size_t at = 0; at + kSeg <= segments.size(); at += kSeg) {
        const uint8_t *s = segments.data() + at;
        hours.push_back(s[0] | (s[1] << 8) | (s[2] << 16));
    }
    return hours;
}

/// A reader over memory that fails every read reaching past @p failFrom.
class FailingReader final : public Airoha::EpoReader {
public:
    FailingReader(const std::vector<uint8_t> &file, size_t failFrom)
        : mInner(file.data(), file.size()), mFailFrom(failFrom) {}

    size_t size() const override { return mInner.size(); }

    bool read(size_t offset, uint8_t *dst, size_t n) override
    {
        if (offset + n > mFailFrom) {
            return false;
        }
        return mInner.read(offset, dst, n);
    }

private:
    MemoryEpoReader mInner;
    size_t mFailFrom;
};

struct Parsed {
    std::vector<uint8_t> file;
    MemoryEpoReader reader;
    std::vector<uint8_t> scratch;
    EpoParser parser;
    bool ok;

    Parsed(std::vector<uint8_t> f, size_t scratchBytes = 16 * kSeg)
        : file(std::move(f)), reader(file.data(), file.size()),
          scratch(scratchBytes), parser(),
          ok(parser.init(reader, scratch.data(), scratch.size())) {}
};

} // namespace

TEST(EpoParser, SevenDayGpsFileSpansAWeek)
{
    Parsed p(fixedFile(svRange(1, 32), 28));   // the size of a real GPS.DAT
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.file.size(), 64512u);

    EXPECT_EQ(p.parser.getFileType(), EpoFileType::ELPO_STANDARD);
    EXPECT_TRUE(p.parser.isSystemType(SystemType::GPS));
    EXPECT_FALSE(p.parser.isSystemType(SystemType::GLONASS));
    EXPECT_EQ(p.parser.getSegmentCount(), 28u * 32u);
    EXPECT_EQ(p.parser.getHeader(), nullptr);

    uint32_t start = 0;
    uint32_t end = 0;
    p.parser.getStartTime(start);
    p.parser.getEndTime(end);
    EXPECT_EQ(start, kStartHour);
    EXPECT_EQ(end, kStartHour + 168);

    EXPECT_TRUE(p.parser.coversTime(weekOf(kStartHour + 167), towOf(kStartHour + 167)));
    EXPECT_FALSE(p.parser.coversTime(weekOf(kStartHour + 168), towOf(kStartHour + 168)));
}

TEST(EpoParser, SelectsOnlyTheBlockHoldingTheTargetHour)
{
    Parsed p(fixedFile(svRange(1, 32), 28));
    ASSERT_TRUE(p.ok);

    // Hour 15 falls in the block that starts at hour 12.
    const uint32_t target = kStartHour + 15;
    EpoBuffer out;
    ASSERT_TRUE(p.parser.selectSegments(SystemType::GPS, weekOf(target), towOf(target), out));

    ASSERT_EQ(out.size(), 32u * kSeg);
    EXPECT_EQ(svsOf(out), svRange(1, 32));
    for (uint32_t hour : hoursOf(out)) {
        EXPECT_EQ(hour, kStartHour + 12);
    }

    // Byte for byte the third block of the file: blocks 0 and 1 come first.
    EXPECT_TRUE(std::equal(out.data(), out.data() + out.size(),
                           p.file.begin() + 2 * 32 * kSeg));
}

TEST(EpoParser, SelectionDoesNotDependOnTheScratchSize)
{
    const std::vector<uint8_t> file = fixedFile(svRange(1, 32), 28);
    const uint32_t target = kStartHour + 100;

    Parsed reference(file, 16 * kSeg);
    ASSERT_TRUE(reference.ok);
    EpoBuffer expected;
    ASSERT_TRUE(reference.parser.selectSegments(SystemType::GPS, weekOf(target), towOf(target), expected));
    ASSERT_EQ(expected.size(), 32u * kSeg);

    // One segment, a size that is not a whole number of segments, and more
    // than the file holds.
    for (size_t scratchBytes : {kSeg, size_t{700}, 64 * kSeg, file.size() + kSeg}) {
        Parsed p(file, scratchBytes);
        ASSERT_TRUE(p.ok) << scratchBytes;
        EpoBuffer out;
        ASSERT_TRUE(p.parser.selectSegments(SystemType::GPS, weekOf(target), towOf(target), out))
                << scratchBytes;
        ASSERT_EQ(out.size(), expected.size()) << scratchBytes;
        EXPECT_TRUE(std::equal(out.data(), out.data() + out.size(), expected.data())) << scratchBytes;
    }
}

TEST(EpoParser, RejectsAScratchBufferSmallerThanASegment)
{
    const std::vector<uint8_t> file = fixedFile(svRange(1, 32), 28);
    MemoryEpoReader reader(file.data(), file.size());
    uint8_t scratch[kSeg - 1];
    EpoParser parser;
    EXPECT_FALSE(parser.init(reader, scratch, sizeof(scratch)));
    EXPECT_FALSE(parser.init(reader, nullptr, 16 * kSeg));
}

TEST(EpoParser, SelectionDoesNotDependOnBlockOrder)
{
    // Every block after the first in reverse. The first stays put: it is where
    // the file's start time is read from.
    std::vector<uint8_t> file = fixedFile(svRange(1, 32), 1);
    for (size_t b = 27; b >= 1; --b) {
        for (uint8_t sv : svRange(1, 32)) {
            appendSegment(file, kStartHour + static_cast<uint32_t>(6 * b), sv);
        }
    }
    Parsed p(std::move(file));
    ASSERT_TRUE(p.ok);

    const uint32_t target = kStartHour + 50;   // block at hour 48
    EpoBuffer out;
    ASSERT_TRUE(p.parser.selectSegments(SystemType::GPS, weekOf(target), towOf(target), out));
    ASSERT_EQ(out.size(), 32u * kSeg);
    EXPECT_EQ(svsOf(out), svRange(1, 32));
    for (uint32_t hour : hoursOf(out)) {
        EXPECT_EQ(hour, kStartHour + 48);
    }
}

TEST(EpoParser, QepoWindowReachesFiveHoursBack)
{
    // A single 6-hour block of 32 GPS satellites is a QEPO file.
    Parsed p(fixedFile(svRange(1, 32), 1));
    ASSERT_TRUE(p.ok);
    EXPECT_EQ(p.parser.getFileType(), EpoFileType::QEPO_FIXED);

    const uint32_t target = kStartHour + 4;
    uint32_t minHour = 0;
    uint32_t maxHour = 0;
    p.parser.segmentWindow(weekOf(target), towOf(target), minHour, maxHour);
    EXPECT_EQ(minHour, target - 5);
    EXPECT_EQ(maxHour, target + 6);

    EpoBuffer out;
    ASSERT_TRUE(p.parser.selectSegments(SystemType::GPS, weekOf(target), towOf(target), out));
    EXPECT_EQ(out.size(), 32u * kSeg);
}

TEST(EpoParser, QepoWindowSaturatesAtTheStartOfGpsTime)
{
    // A QEPO block at GPS hour 0, asked for at hour 2: five hours back would
    // be before the epoch.
    std::vector<uint8_t> file;
    for (uint8_t sv : svRange(1, 32)) {
        appendSegment(file, 0, sv);
    }
    Parsed p(std::move(file));
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.parser.getFileType(), EpoFileType::QEPO_FIXED);
    ASSERT_TRUE(p.parser.coversTime(0, towOf(2)));

    uint32_t minHour = 1;
    uint32_t maxHour = 0;
    p.parser.segmentWindow(0, towOf(2), minHour, maxHour);
    EXPECT_EQ(minHour, 0u);
    EXPECT_EQ(maxHour, 8u);

    EpoBuffer out;
    ASSERT_TRUE(p.parser.selectSegments(SystemType::GPS, 0, towOf(2), out));
    EXPECT_EQ(out.size(), 32u * kSeg);
}

TEST(EpoParser, CombinedGpsGlonassFileSelectsOneSystem)
{
    std::vector<uint8_t> svs = svRange(1, 32);
    const std::vector<uint8_t> glonass = svRange(65, 24);
    svs.insert(svs.end(), glonass.begin(), glonass.end());

    Parsed p(fixedFile(svs, 28));
    ASSERT_TRUE(p.ok);
    EXPECT_TRUE(p.parser.isSystemType(SystemType::GPS));
    EXPECT_TRUE(p.parser.isSystemType(SystemType::GLONASS));

    const uint32_t target = kStartHour + 30;
    EpoBuffer out;
    ASSERT_TRUE(p.parser.selectSegments(SystemType::GLONASS, weekOf(target), towOf(target), out));
    EXPECT_EQ(svsOf(out), glonass);
}

TEST(EpoParser, BeidouHeaderIsCopiedOutAndItsSatellitesCounted)
{
    // 29 BeiDou satellites in 28 blocks: the size of a real BD.DAT.
    const std::vector<uint8_t> svs = svRange(201, 29);
    Parsed p(variableFile(255, svs, 28));
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.file.size(), 58536u);

    EXPECT_EQ(p.parser.getFileType(), EpoFileType::ELPO_VARIABLE);
    EXPECT_TRUE(p.parser.isSystemType(SystemType::BEIDOU));
    EXPECT_EQ(p.parser.getSegmentCount(), 28u * 29u);

    const uint8_t *header = p.parser.getHeader();
    ASSERT_NE(header, nullptr);
    EXPECT_TRUE(std::equal(header, header + kSeg, p.file.begin()));

    uint32_t start = 0;
    uint32_t end = 0;
    p.parser.getStartTime(start);
    p.parser.getEndTime(end);
    EXPECT_EQ(start, kStartHour);
    EXPECT_EQ(end, kStartHour + 168);

    const uint32_t target = kStartHour + 7;
    EpoBuffer out;
    ASSERT_TRUE(p.parser.selectSegments(SystemType::BEIDOU, weekOf(target), towOf(target), out));
    EXPECT_EQ(svsOf(out), svs);
    for (uint32_t hour : hoursOf(out)) {
        EXPECT_EQ(hour, kStartHour + 6);
    }
}

TEST(EpoParser, AnotherSystemSelectsNothing)
{
    Parsed p(fixedFile(svRange(1, 32), 28));
    ASSERT_TRUE(p.ok);

    const uint32_t target = kStartHour + 20;
    EpoBuffer out;
    EXPECT_TRUE(p.parser.selectSegments(SystemType::QZSS, weekOf(target), towOf(target), out));
    EXPECT_TRUE(out.empty());
}

TEST(EpoParser, RejectsAFileThatIsNotWholeSegments)
{
    std::vector<uint8_t> file = fixedFile(svRange(1, 32), 28);
    file.push_back(0);
    Parsed p(std::move(file));
    EXPECT_FALSE(p.ok);
}

TEST(EpoParser, AReadFailureIsReportedNotSent)
{
    const std::vector<uint8_t> file = fixedFile(svRange(1, 32), 28);

    // Detection reads at most the first 100 segments of a fixed file, so a
    // failure beyond them leaves init() succeeding and the selection failing.
    FailingReader reader(file, 200 * kSeg);
    std::vector<uint8_t> scratch(16 * kSeg);
    EpoParser parser;
    ASSERT_TRUE(parser.init(reader, scratch.data(), scratch.size()));

    const uint32_t target = kStartHour + 20;
    EpoBuffer out;
    EXPECT_FALSE(parser.selectSegments(SystemType::GPS, weekOf(target), towOf(target), out));

    // And a failure inside the detection range fails init().
    FailingReader early(file, 10 * kSeg);
    EpoParser parser2;
    EXPECT_FALSE(parser2.init(early, scratch.data(), scratch.size()));
}

TEST(EpoParser, MemoryReaderRefusesReadsPastTheEnd)
{
    const uint8_t data[4] = {1, 2, 3, 4};
    MemoryEpoReader reader(data, sizeof(data));
    uint8_t out[4] {};
    EXPECT_TRUE(reader.read(0, out, 4));
    EXPECT_TRUE(reader.read(4, out, 0));
    EXPECT_FALSE(reader.read(1, out, 4));
    EXPECT_FALSE(reader.read(5, out, 0));
}
