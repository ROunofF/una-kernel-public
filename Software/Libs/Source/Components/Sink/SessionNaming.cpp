/**
 ******************************************************************************
 * @file    SessionNaming.cpp
 * @brief   Session-file naming + retention policy.
 ******************************************************************************
 */

#include "Sink/SessionNaming.hpp"
#include "Sink/SessionFiles.hpp"

#include <cstdio>
#include <ctime>

namespace Sink {

void SessionNaming::configure(const char* dir, const char* base, const char* id,
                              const char* ext, uint32_t keepNewest)
{
    mDir = dir ? dir : "";

    // Fixed prefix up to and including the separator before NNNN. SessionFiles
    // matches on it, and the file name is built from it, so both sides agree.
    mPrefix  = base ? base : "";
    mPrefix += '_';
    if (id) {
        mPrefix += id;
    }
    mPrefix += '_';

    mSuffix  = '.';
    mSuffix += ext ? ext : "";

    mKeepNewest = keepNewest;
}

void SessionNaming::captureSession()
{
    std::time_t utc = std::time(nullptr);
    std::tm     lt {};
    localtime_r(&utc, &lt);

    // Bound each field so the result provably fits mSessionTs (15 chars + NUL).
    std::snprintf(mSessionTs, sizeof(mSessionTs), "%04u%02u%02uT%02u%02u%02u",
                  static_cast<unsigned>(lt.tm_year + 1900) % 10000u,
                  static_cast<unsigned>(lt.tm_mon + 1)     % 100u,
                  static_cast<unsigned>(lt.tm_mday)        % 100u,
                  static_cast<unsigned>(lt.tm_hour)        % 100u,
                  static_cast<unsigned>(lt.tm_min)         % 100u,
                  static_cast<unsigned>(lt.tm_sec)         % 100u);
}

bool SessionNaming::nextPath(char* out, size_t outSize)
{
    if (out == nullptr || outSize == 0) {
        return false;
    }

    // One directory pass gives both the next index and the pruning of old
    // sessions. NNNN drives ordering; the timestamp is the session-start label
    // shared by every segment (captureSession(), set once).
    const uint32_t n = SessionFiles::prepare(mDir.c_str(), mPrefix.c_str(),
                                             mSuffix.c_str(), mKeepNewest);

    int len = std::snprintf(out, outSize, "%s%s%04u_%s%s",
                            mDir.c_str(), mPrefix.c_str(), static_cast<unsigned>(n),
                            mSessionTs, mSuffix.c_str());

    return len > 0 && static_cast<size_t>(len) < outSize;
}

}  // namespace Sink
