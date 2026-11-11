/**
 ******************************************************************************
 * @file    JsonFileStorage.cpp
 * @brief   IDailyStorage implementation backed by FS + JSON / binary CRC.
 *
 *          The recovery payload carries (DailyDate + DailyMetrics).
 *          DailyDate is serialised explicitly because DailyMetrics does
 *          not embed it; together they describe a single day's state.
 ******************************************************************************
 */

#include "DailyHealth/JsonFileStorage.hpp"

#define LOG_MODULE_PRX      "DH.Storage"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cstdio>
#include <cstring>

#include "Directory.hpp"
#include "ScopeGuard.hpp"
#include "Crc/Crc32.hpp"
#include "SDK/JSON/JsonStreamWriter.hpp"
#include "SDK/JSON/JsonStreamReader.hpp"

namespace DailyHealth
{

JsonFileStorage::JsonFileStorage(const char* dirPath, const char* tmpFilePath)
    : mDirPath(dirPath)
    , mTmpFilePath(tmpFilePath)
    , mReadOnly(false)
{
    static_assert(sizeof(DailyDate) == 8, "DailyDate layout drifted");
}

// ============================================================================
// Path helpers
// ============================================================================

void JsonFileStorage::buildArchiveDirPath(int year, int mon)
{
    std::snprintf(mBuffer, sizeof(mBuffer), "%s%04u%02u/",
                  mDirPath,
                  static_cast<unsigned>(year),
                  static_cast<unsigned>(mon));
}

void JsonFileStorage::buildArchiveFilePath(int year, int mon, int mday)
{
    std::snprintf(mBuffer, sizeof(mBuffer),
                  "%s%04u%02u/dh_%04u%02u%02u.json",
                  mDirPath,
                  static_cast<unsigned>(year),
                  static_cast<unsigned>(mon),
                  static_cast<unsigned>(year),
                  static_cast<unsigned>(mon),
                  static_cast<unsigned>(mday));
}

// ============================================================================
// Daily archive (JSON)
// ============================================================================

bool JsonFileStorage::writeRecordJson(const char* path,
                                      const DailyRecord& r,
                                      const DailyHrMatrix& hr)
{
    mFile.setPath(path);

    if (!mFile.open(true, true)) {
        LOG_ERROR("Failed to open archive file for write: %s\n", path);
        return false;
    }

    auto sg = MakeScopeGuard([this]() {
        mFile.close();
    });

    SDK::JsonStreamWriter writer(mBuffer, sizeof(mBuffer));

    writer.startMap();
    writer.add("dailySteps",           r.steps);
    writer.add("dailyFloorsUp",        r.floorsUp);
    writer.add("dailyFloorsDown",      r.floorsDown);
    writer.add("dailyActivityMinutes", r.activityMinutes);
    writer.add("restingHeartRate",     r.restingHeartRate);
    writer.add("averageHeartRate",     r.averageHeartRate);
    // hrPerMinute is the last key so loadRecord can stop early when the
    // scalars it cares about have all been seen.
    writer.startArray("hrPerMinute");
    for (uint8_t h = 0; h < 24; ++h) {
        for (uint8_t m = 0; m < 60; ++m) {
            writer.add(hr.cells[h][m]);
        }
    }
    writer.endArray();
    writer.endMap();

    if (writer.isError()) {
        LOG_ERROR("Failed to serialise daily record JSON\n");
        return false;
    }

    size_t bw = 0;
    if (!mFile.write(mBuffer, std::strlen(mBuffer), bw)) {
        LOG_ERROR("Failed to write daily record\n");
        return false;
    }

    LOG_DEBUG("Daily record written: %s\n", path);
    return true;
}

bool JsonFileStorage::readRecordJson(const char* path, DailyRecord& out)
{
    mFile.setPath(path);

    if (!mFile.exist()) {
        return false;
    }

    const size_t len = mFile.size();
    if (len == 0 || len > sizeof(mBuffer)) {
        if (len > 0) {
            LOG_ERROR("Buffer too small for daily record (%u bytes)\n",
                      static_cast<unsigned>(len));
        }
        return false;
    }

    if (!mFile.open()) {
        LOG_ERROR("Failed to open daily record: %s\n", path);
        return false;
    }

    auto sg = MakeScopeGuard([this]() {
        mFile.close();
    });

    size_t br = 0;
    if (!mFile.read(mBuffer, len, br) || br == 0) {
        return false;
    }

    SDK::JsonStreamReader r(mBuffer, len);
    if (!r.validate()) {
        LOG_ERROR("Daily record JSON invalid: %s\n", path);
        return false;
    }

    r.get("dailySteps",           out.steps);
    r.get("dailyFloorsUp",        out.floorsUp);
    r.get("dailyFloorsDown",      out.floorsDown);
    r.get("dailyActivityMinutes", out.activityMinutes);
    r.get("restingHeartRate",     out.restingHeartRate);
    r.get("averageHeartRate",     out.averageHeartRate);
    return true;
}

bool JsonFileStorage::readHrSeriesJson(const char* path, DailyHrMatrix& out)
{
    mFile.setPath(path);

    if (!mFile.exist()) {
        return false;
    }

    const size_t len = mFile.size();
    // strict '<' here (rather than the '<=' used by readRecordJson) so we
    // always have one slot left for an explicit NUL terminator, which the
    // strstr() scan below relies on.
    if (len == 0 || len >= sizeof(mBuffer)) {
        return false;
    }

    if (!mFile.open()) {
        LOG_ERROR("Failed to open daily record: %s\n", path);
        return false;
    }

    auto sg = MakeScopeGuard([this]() {
        mFile.close();
    });

    size_t br = 0;
    if (!mFile.read(mBuffer, len, br) || br == 0) {
        return false;
    }
    // strstr() below needs a NUL terminator. The size check above
    // guarantees len < sizeof(mBuffer), so this slot is in bounds.
    mBuffer[len] = '\0';

    // Validate first so we never inline-parse a corrupt buffer.
    SDK::JsonStreamReader r(mBuffer, len);
    if (!r.validate()) {
        LOG_ERROR("Daily record JSON invalid: %s\n", path);
        return false;
    }

    // Locate the hrPerMinute array. JsonStreamReader's element-by-element
    // access is O(N) per lookup, so 1440 calls would be O(N^2) on a 6 KB
    // buffer; a single strstr + manual digit walk keeps it linear.
    static constexpr char kKey[] = "\"hrPerMinute\":[";
    const char* end = mBuffer + len;
    const char* p   = std::strstr(mBuffer, kKey);
    if (p == nullptr) {
        return false;
    }
    p += sizeof(kKey) - 1;

    constexpr size_t kCount = 24u * 60u;
    uint8_t* dst = &out.cells[0][0];
    size_t   i   = 0;

    while (i < kCount && p < end) {
        while (p < end && (*p == ' ' || *p == '\t' ||
                           *p == '\n' || *p == '\r' || *p == ','))
        {
            ++p;
        }
        if (p >= end || *p == ']') {
            break;
        }

        unsigned val = 0;
        while (p < end && *p >= '0' && *p <= '9') {
            val = val * 10u + static_cast<unsigned>(*p - '0');
            ++p;
        }
        dst[i++] = (val > 255u) ? 255u : static_cast<uint8_t>(val);
    }

    if (i != kCount) {
        LOG_WARNING("hrPerMinute had %u entries, expected %u\n",
                    static_cast<unsigned>(i),
                    static_cast<unsigned>(kCount));
        return false;
    }
    return true;
}

bool JsonFileStorage::loadRecord(std::tm date, DailyRecord& out)
{
    out = DailyRecord{};

    if (mReadOnly) {
        return false;
    }

    buildArchiveFilePath(date.tm_year + 1900,
                         date.tm_mon + 1,
                         date.tm_mday);

    return readRecordJson(mBuffer, out);
}

bool JsonFileStorage::loadHrSeries(std::tm date, DailyHrMatrix& out)
{
    out = DailyHrMatrix{};

    if (mReadOnly) {
        return false;
    }

    buildArchiveFilePath(date.tm_year + 1900,
                         date.tm_mon + 1,
                         date.tm_mday);

    return readHrSeriesJson(mBuffer, out);
}

bool JsonFileStorage::saveRecord(std::tm date,
                                 const DailyRecord& summary,
                                 const DailyHrMatrix& hr)
{
    if (mReadOnly) {
        return false;
    }

    buildArchiveDirPath(date.tm_year + 1900, date.tm_mon + 1);

    if (!FS::Directory::Create(mBuffer)) {
        LOG_ERROR("Failed to create archive dir: %s\n", mBuffer);
        return false;
    }

    buildArchiveFilePath(date.tm_year + 1900,
                         date.tm_mon + 1,
                         date.tm_mday);

    return writeRecordJson(mBuffer, summary, hr);
}

// ============================================================================
// In-progress recovery (binary + CRC32)
// ============================================================================
//
// Layout: [DailyDate date][DailyMetrics m][uint32_t crc32]
// crc32 covers the (date + m) bytes only.
//
// DailyDate has a fixed, layout-stable representation (see Types.hpp);
// DailyMetrics is plain-data so any padding the compiler inserts is
// consistent between save and load on the same target.

namespace {

constexpr size_t kRecDateSize  = sizeof(DailyDate);
constexpr size_t kRecMetSize   = sizeof(DailyMetrics);
constexpr size_t kRecCrcSize   = sizeof(uint32_t);
constexpr size_t kRecPayloadSz = kRecDateSize + kRecMetSize;
constexpr size_t kRecTotal     = kRecPayloadSz + kRecCrcSize;

} // namespace

bool JsonFileStorage::saveCurrent(std::tm date, const DailyMetrics& m)
{
    if (mReadOnly) {
        return false;
    }

    static_assert(kRecTotal <= kBufferSize,
                  "DailyHealth recovery payload exceeds buffer size");

    const DailyDate dd = DailyDate::fromTm(date);

    std::memcpy(&mBuffer[0],             &dd, kRecDateSize);
    std::memcpy(&mBuffer[kRecDateSize],  &m,  kRecMetSize);

    const uint32_t crc =
        CRC32::calculate(reinterpret_cast<const uint8_t*>(mBuffer),
                         kRecPayloadSz);
    std::memcpy(&mBuffer[kRecPayloadSz], &crc, kRecCrcSize);

    mFile.setPath(mTmpFilePath);
    if (!mFile.open(true, true)) {
        LOG_ERROR("Failed to open recovery file for write\n");
        return false;
    }

    auto sg = MakeScopeGuard([this]() {
        mFile.close();
    });

    size_t bw = 0;
    if (!mFile.write(mBuffer, kRecTotal, bw)) {
        LOG_ERROR("Failed to write recovery payload\n");
        return false;
    }
    return true;
}

bool JsonFileStorage::loadCurrent(DailyMetrics& outMetrics, std::tm& outDate)
{
    if (!mFile.Exist(mTmpFilePath)) {
        return false;
    }

    static_assert(kRecTotal <= kBufferSize,
                  "DailyHealth recovery payload exceeds buffer size");

    mFile.setPath(mTmpFilePath);

    const size_t len = mFile.size();
    if (len != kRecTotal) {
        LOG_WARNING("Recovery file size mismatch: have %u, expect %u\n",
                    static_cast<unsigned>(len),
                    static_cast<unsigned>(kRecTotal));
        return false;
    }

    if (!mFile.open()) {
        LOG_ERROR("Failed to open recovery file\n");
        return false;
    }

    auto sg = MakeScopeGuard([this]() {
        mFile.close();
    });

    size_t br = 0;
    if (!mFile.read(mBuffer, len, br) || br != kRecTotal) {
        return false;
    }

    uint32_t storedCrc = 0;
    std::memcpy(&storedCrc, &mBuffer[kRecPayloadSz], kRecCrcSize);

    const uint32_t crc =
        CRC32::calculate(reinterpret_cast<const uint8_t*>(mBuffer),
                         kRecPayloadSz);
    if (crc != storedCrc) {
        LOG_WARNING("Recovery file CRC mismatch\n");
        return false;
    }

    DailyDate dd{};
    std::memcpy(&dd,         &mBuffer[0],            kRecDateSize);
    std::memcpy(&outMetrics, &mBuffer[kRecDateSize], kRecMetSize);
    outDate = dd.toTm();
    return true;
}

// ============================================================================
// Erase / MSD / flush
// ============================================================================

bool JsonFileStorage::eraseCurrent()
{
    if (mReadOnly) {
        return false;
    }
    if (!FS::File::Exist(mTmpFilePath)) {
        return true;
    }
    return FS::File::Remove(mTmpFilePath);
}

bool JsonFileStorage::eraseAll()
{
    if (mReadOnly) {
        return false;
    }

    if (FS::File::Exist(mTmpFilePath)) {
        FS::File::Remove(mTmpFilePath);
    }
    if (FS::Directory::Exist(mDirPath)) {
        FS::Directory::RemoveRecursively(mDirPath, nullptr, false);
    }
    return true;
}

void JsonFileStorage::setReadOnly(bool readOnly)
{
    mReadOnly = readOnly;
}

} // namespace DailyHealth
