/**
 ******************************************************************************
 * @file    SessionFiles.hpp
 * @brief   Session-file directory helper: next index + count-based retention.
 ******************************************************************************
 */

#ifndef SINK_SESSION_FILES_HPP
#define SINK_SESSION_FILES_HPP

#include <cstdint>

namespace Sink {
namespace SessionFiles {

/**
 * @brief Prepare a log directory for a new session file.
 *
 * Scans @p dir once for files named "<prefix><NNNN>...<suffix>", where NNNN is
 * the leading integer right after @p prefix (terminated by '_' or by the
 * suffix). NNNN is the ordering key, not the file timestamp, so the ordering
 * holds even when the RTC is invalid or jumps (timezone / DST).
 *
 * Returns max(NNNN) + 1 as the index for the next session. When
 * @p keepNewest > 0 it also deletes the oldest sessions (lowest NNNN) so that
 * at most keepNewest-1 remain, leaving room for the caller's new file to make
 * keepNewest. To bound RAM and latency only a few files are removed per call,
 * so a large backlog drains over successive sessions.
 *
 * Cheap by construction: the directory handle is the only FatFs lock held, and
 * it is released before any deletion; the files being deleted are never opened.
 *
 * @param dir         Directory path (e.g. "2:/BatteryLog/").
 * @param prefix      Fixed name prefix up to and including the separator before
 *                    NNNN (e.g. "battery_123456_").
 * @param suffix      File suffix to match (e.g. ".csv").
 * @param keepNewest  Sessions to keep including the new one; 0 disables pruning.
 * @return Index for the new session file (>= 1).
 */
uint32_t prepare(const char* dir, const char* prefix, const char* suffix,
                 uint32_t keepNewest);

}  // namespace SessionFiles
}  // namespace Sink

#endif  // SINK_SESSION_FILES_HPP
