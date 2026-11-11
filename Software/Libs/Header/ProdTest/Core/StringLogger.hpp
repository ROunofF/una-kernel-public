/**
 ******************************************************************************
 * @file    StringLogger.hpp
 * @brief   Heap-based string history logger: printf-like capture +
 *          in-memory storage for later retrieval/printing.
 *
 * This module provides a lightweight helper that formats messages (printf-style),
 * stores them as std::string nodes in a std::list (optionally bounded by a
 * maximum number of entries), and returns a stable const char* pointer to the
 * stored string so it can be used immediately in logging macros (e.g. LOG_xxx).
 *
 * Usage pattern:
 *   LOG_INFO("%s", slog.print("Battery: %u mV\n", mv));
 *
 * Notes:
 * - Returned pointers remain valid while the corresponding list node exists.
 *   If @ref mMaxEntries is non-zero, the oldest entries are evicted (pop_front)
 *   to keep the history bounded; after eviction, previously returned pointers
 *   to those evicted nodes become invalid.
 * - Thread-safety: this implementation is NOT synchronized. Guard calls with
 *   a mutex/critical section if accessed concurrently.
 *
 ******************************************************************************
 */

#ifndef __STRING_LOGGER_HPP
#define __STRING_LOGGER_HPP

#include <list>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <cstddef>

class StringLogger {
public:
    explicit StringLogger(std::size_t maxEntries = 0);
    ~StringLogger();

    const char* print(const char* fmt, ...);

    const std::list<std::string>& lines() const;

private:
    const char* storeAndGetPtr(std::string&& s);

    std::list<std::string> mLines;
    std::size_t            mMaxEntries;
};

#endif  // __STRING_LOGGER_HPP
