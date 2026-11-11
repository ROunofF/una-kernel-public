/**
 ******************************************************************************
 * @file    StringLogger.cpp
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

#include "ProdTest/Core/StringLogger.hpp"

#include <algorithm>

/**
 * @brief Construct a new StringLogger.
 * @param maxEntries Maximum number of stored lines; 0 means "unbounded".
 */
StringLogger::StringLogger(std::size_t maxEntries)
    : mLines()
    , mMaxEntries(maxEntries)
{}

/**
 * @brief Destroy the StringLogger and release stored messages.
 */
StringLogger::~StringLogger()
{
    mLines.clear();
}

/**
 * @brief Format and store a message, returning a stable C string pointer.
 *
 * The function formats the message with printf-like semantics, stores it
 * into the internal list, and returns @c c_str() of the newly stored string.
 * This pointer is suitable for immediate use in logging macros, e.g.:
 *
 * @code
 *   LOG_INFO("%s", slog.print("Hello %d\n", 123));
 * @endcode
 *
 * @param fmt  printf-like format string.
 * @param ...  Variadic arguments for @p fmt.
 * @return const char* Pointer to stored, NUL-terminated string.
 *                     Becomes invalid if that entry is later evicted or cleared.
 */
const char* StringLogger::print(const char* fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = std::vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);

    if (n < 0) {
        // Fallback: format into a small stack buffer if sizing failed.
        char buf[256];

        va_start(ap2, fmt);
        int m = std::vsnprintf(buf, sizeof(buf), fmt, ap2);
        va_end(ap2);

        if (m < 0) {
            return storeAndGetPtr(std::string{});
        }

        return storeAndGetPtr(std::string(buf, static_cast<std::size_t>(std::min(m, static_cast<int>(sizeof(buf) - 1)))));
    }

    std::string s;
    s.resize(static_cast<std::size_t>(n));
    std::vsnprintf(s.data(), s.size() + 1, fmt, ap2);
    va_end(ap2);

    return storeAndGetPtr(std::move(s));
}

/**
 * @brief Get read-only access to the underlying list of messages.
 * @return const std::list<std::string>& Reference to the internal container.
 *
 * @warning Modifying the container (e.g., erasing elements) will invalidate
 *          pointers returned earlier by @ref print.
 */
const std::list<std::string>& StringLogger::lines() const
{
    return mLines;
}

/**
 * @brief Store the given string into the history and return a stable C pointer.
 *
 * If @ref mMaxEntries is non-zero and the container is full, the oldest entry
 * is removed before pushing the new one.
 *
 * @param s Rvalue string to store (moved into the container).
 * @return const char* Pointer to the stored string's internal buffer.
 */
const char* StringLogger::storeAndGetPtr(std::string&& s)
{
    if (mMaxEntries && mLines.size() >= mMaxEntries) {
        mLines.pop_front();
    }

    mLines.push_back(std::move(s));

    return mLines.back().c_str();
}
