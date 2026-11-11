/**
 ******************************************************************************
 * @file    SessionFilesDetail.hpp
 * @brief   Pure helpers behind SessionFiles::prepare (parse + retention policy).
 *
 * @details
 * Header-only and free of any filesystem/RTOS dependency, so the risky logic -
 * filename parsing and the count-based retention math - is unit-testable on the
 * host. SessionFiles.cpp supplies the directory scan and the deletions.
 ******************************************************************************
 */

#ifndef SINK_SESSION_FILES_DETAIL_HPP
#define SINK_SESSION_FILES_DETAIL_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

namespace Sink {
namespace SessionFiles {
namespace detail {

// Oldest sessions removed in a single prepare() call. Steady state needs 1 (one
// new file per session); a higher cap only matters when draining a backlog left
// by older firmware, which it does a few files at a time. Bounds both the
// cached-victim footprint and the per-call latency.
constexpr uint32_t kMaxDeletePerCall = 8;

/**
 * @brief Parse the leading NNNN of a candidate file name.
 *
 * Matches "<prefix><digits>(_|.)..." ending in @p suffix. The digits are the
 * whole field for the legacy "<prefix><N><suffix>" form, or the first field
 * for "<prefix><N>_<rest><suffix>" - both are accepted so retention works
 * across the naming migration. NNNN must be a standalone field (terminated by
 * '_' or by the suffix) and non-zero.
 */
inline bool parseIndex(const char* name, size_t nameLen,
                       const char* prefix, size_t prefixLen,
                       const char* suffix, size_t suffixLen,
                       uint32_t& outIndex)
{
    if (nameLen < prefixLen + 1 + suffixLen) {
        return false;
    }
    if (std::strncmp(name, prefix, prefixLen) != 0) {
        return false;
    }
    if (std::strncmp(name + nameLen - suffixLen, suffix, suffixLen) != 0) {
        return false;
    }

    const char* numStart = name + prefixLen;
    char*       parseEnd = nullptr;
    const unsigned long n = std::strtoul(numStart, &parseEnd, 10);
    if (parseEnd == numStart) {
        return false;                       // no leading digit
    }
    if (*parseEnd != '_' && *parseEnd != '.') {
        return false;                       // NNNN is not a standalone field
    }
    if (n == 0) {
        return false;
    }

    outIndex = static_cast<uint32_t>(n);
    return true;
}

/**
 * @brief Bounded cache of the lowest-index files seen during a scan.
 *
 * Keeps up to kMaxDeletePerCall entries sorted ascending by index, so the
 * oldest sessions can be deleted without holding every directory entry in RAM.
 * Names are stored alongside (on the heap) because they cannot be reconstructed
 * from the index alone (the free-form timestamp/payload tail varies).
 */
struct VictimCache {
    struct Entry {
        uint32_t    index = 0;
        std::string name;
    };

    Entry    entries[kMaxDeletePerCall];
    uint32_t tracked = 0;

    void consider(uint32_t index, const char* name)
    {
        const bool full = (tracked == kMaxDeletePerCall);
        if (full && index >= entries[tracked - 1].index) {
            return;                         // not among the smallest we keep
        }
        uint32_t pos = full ? (tracked - 1) : (tracked++);
        while (pos > 0 && entries[pos - 1].index > index) {
            entries[pos] = std::move(entries[pos - 1]);
            --pos;
        }
        entries[pos].index = index;
        entries[pos].name  = name ? name : "";
    }
};

/**
 * @brief How many of the oldest files to delete so the new file lands inside
 *        the budget: keep keepNewest-1 existing, capped to what was cached.
 *
 * @param count       Matching files found.
 * @param keepNewest  Sessions to retain incl. the new one; 0 disables pruning.
 * @param tracked     Entries available in the victim cache.
 */
inline uint32_t deleteCount(uint32_t count, uint32_t keepNewest, uint32_t tracked)
{
    if (keepNewest == 0 || count < keepNewest) {
        return 0;
    }
    const uint32_t n = count - (keepNewest - 1);
    return n > tracked ? tracked : n;
}

}  // namespace detail
}  // namespace SessionFiles
}  // namespace Sink

#endif  // SINK_SESSION_FILES_DETAIL_HPP
