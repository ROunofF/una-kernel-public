#include <gtest/gtest.h>

#include "Sink/SessionFilesDetail.hpp"

#include <cstdint>
#include <string>

using Sink::SessionFiles::detail::deleteCount;
using Sink::SessionFiles::detail::kMaxDeletePerCall;
using Sink::SessionFiles::detail::parseIndex;
using Sink::SessionFiles::detail::VictimCache;

namespace {

// Convenience wrapper over the length-taking parseIndex.
bool parse(const std::string& name, const std::string& prefix,
           const std::string& suffix, uint32_t& out)
{
    return parseIndex(name.c_str(), name.size(),
                      prefix.c_str(), prefix.size(),
                      suffix.c_str(), suffix.size(), out);
}

constexpr char kPrefix[] = "battery_123456_";
constexpr char kSuffix[] = ".csv";

}  // namespace

// ---------------------------------------------------------------- parseIndex

TEST(SessionFilesParse, NewFormatIndexBeforeTimestamp) {
    uint32_t n = 0;
    EXPECT_TRUE(parse("battery_123456_0007_20260613T120000.csv", kPrefix, kSuffix, n));
    EXPECT_EQ(n, 7u);
}

TEST(SessionFilesParse, LegacyFormatBareIndex) {
    uint32_t n = 0;
    EXPECT_TRUE(parse("battery_123456_12.csv", kPrefix, kSuffix, n));
    EXPECT_EQ(n, 12u);
}

TEST(SessionFilesParse, LeadingZerosParsed) {
    uint32_t n = 0;
    EXPECT_TRUE(parse("battery_123456_0042_x.csv", kPrefix, kSuffix, n));
    EXPECT_EQ(n, 42u);
}

TEST(SessionFilesParse, DeviceIdDigitsNotConfusedWithIndex) {
    // The id (with digits) is part of the fixed prefix; NNNN is the field after.
    uint32_t n = 0;
    EXPECT_TRUE(parse("battery_12345_0009_ts.csv", "battery_12345_", kSuffix, n));
    EXPECT_EQ(n, 9u);
}

TEST(SessionFilesParse, RejectNoLeadingDigit) {
    uint32_t n = 123;
    EXPECT_FALSE(parse("battery_123456_x_ts.csv", kPrefix, kSuffix, n));
}

TEST(SessionFilesParse, RejectIndexNotStandaloneField) {
    // Digits must be terminated by '_' or '.', not by another character.
    uint32_t n = 123;
    EXPECT_FALSE(parse("battery_123456_7a.csv", kPrefix, kSuffix, n));
}

TEST(SessionFilesParse, RejectZeroIndex) {
    uint32_t n = 123;
    EXPECT_FALSE(parse("battery_123456_0_ts.csv", kPrefix, kSuffix, n));
}

TEST(SessionFilesParse, RejectWrongSuffix) {
    uint32_t n = 123;
    EXPECT_FALSE(parse("battery_123456_7.txt", kPrefix, kSuffix, n));
}

TEST(SessionFilesParse, RejectWrongPrefix) {
    uint32_t n = 123;
    EXPECT_FALSE(parse("gps_123456_7.csv", kPrefix, kSuffix, n));
}

TEST(SessionFilesParse, RejectNoDigitsBetweenPrefixAndSuffix) {
    uint32_t n = 123;
    EXPECT_FALSE(parse("battery_123456_.csv", kPrefix, kSuffix, n));
}

// --------------------------------------------------------------- deleteCount

TEST(SessionFilesRetention, EmptyDirectoryDeletesNothing) {
    EXPECT_EQ(deleteCount(0, 14, 0), 0u);
}

TEST(SessionFilesRetention, BelowBudgetDeletesNothing) {
    EXPECT_EQ(deleteCount(13, 14, 13), 0u);
}

TEST(SessionFilesRetention, AtBudgetDeletesOne) {
    EXPECT_EQ(deleteCount(14, 14, 8), 1u);
}

TEST(SessionFilesRetention, AboveBudgetDeletesExcessPlusOne) {
    EXPECT_EQ(deleteCount(20, 14, 8), 7u);   // 20 - (14 - 1)
}

TEST(SessionFilesRetention, BacklogClampedToCacheCapacity) {
    EXPECT_EQ(deleteCount(40, 14, 8), 8u);   // 27 wanted, only 8 cached -> drains over calls
}

TEST(SessionFilesRetention, KeepNewestZeroDisablesPruning) {
    EXPECT_EQ(deleteCount(100, 0, 8), 0u);
}

TEST(SessionFilesRetention, KeepNewestOneDropsAllExisting) {
    EXPECT_EQ(deleteCount(5, 1, 4), 4u);     // keep only the live file; 5 wanted, 4 cached
}

// --------------------------------------------------------------- VictimCache

TEST(SessionFilesVictimCache, KeepsSmallestSortedWhenOverCapacity) {
    VictimCache c;
    const uint32_t order[] = {5, 1, 9, 3, 7, 2, 8, 4, 6, 10};
    for (uint32_t v : order) {
        c.consider(v, "n");
    }
    ASSERT_EQ(c.tracked, kMaxDeletePerCall);          // 8
    for (uint32_t i = 0; i < c.tracked; ++i) {
        EXPECT_EQ(c.entries[i].index, i + 1u);        // smallest 8 are 1..8, ascending
    }
}

TEST(SessionFilesVictimCache, SortsAscendingUnderCapacity) {
    VictimCache c;
    c.consider(30, "a");
    c.consider(10, "b");
    c.consider(20, "c");
    ASSERT_EQ(c.tracked, 3u);
    EXPECT_EQ(c.entries[0].index, 10u);
    EXPECT_EQ(c.entries[1].index, 20u);
    EXPECT_EQ(c.entries[2].index, 30u);
}

TEST(SessionFilesVictimCache, NamesTrackTheirIndices) {
    VictimCache c;
    c.consider(2, "two");
    c.consider(1, "one");
    ASSERT_EQ(c.tracked, 2u);
    EXPECT_EQ(c.entries[0].name, "one");
    EXPECT_EQ(c.entries[1].name, "two");
}
