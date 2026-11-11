/**
 ******************************************************************************
 * @file    LogBuffer.cpp
 * @brief   Self-contained no-init ring buffer for system logging.
 ******************************************************************************
 */

#include "LogBuffer/LogBuffer.hpp"

#include "main.h"   // CMSIS intrinsics (__get_PRIMASK / __disable_irq / __set_PRIMASK)

#include <cstring>

#define LOGBUFFER_MAGIC  (0x4C4F4743u)   /**< "LOGC" - bumped when the no-init layout changed. */

/**
 * @brief No-init storage descriptor + data, preserved across warm reset.
 *
 * Placed in .log_buffer (NOLOAD) so startup code does not zero it. Validity is
 * established by @ref LogBufferStorage::magic; on a cold boot the contents are
 * random and the magic check fails, triggering a lazy reset on first access.
 *
 * @ref total is the single source of truth: head and size are derived from it
 * (head = total % capacity, size = min(total, capacity)).
 */
typedef struct {
    uint32_t magic;
    uint32_t capacity;
    uint64_t total;                     /**< Monotonic count of bytes ever written. */
    uint8_t  data[LOGBUFFER_CAPACITY];
} LogBufferStorage;

static __attribute__((section(".log_buffer"))) __attribute__((used))
LogBufferStorage s;

/// Notify callback (runtime state, intentionally not in the no-init region).
static LogBuffer::NotifyCb sNotify;
static void*               sNotifyCtx;

/**
 * @brief Lazily validate the no-init region; reset header on cold boot.
 *
 * The data region itself is not cleared: validity is governed by @ref total,
 * so there is no need to memset 64 KiB at boot.
 */
static inline void ensureInit(void)
{
    if (s.magic != LOGBUFFER_MAGIC || s.capacity != LOGBUFFER_CAPACITY) {
        s.magic    = LOGBUFFER_MAGIC;
        s.capacity = LOGBUFFER_CAPACITY;
        s.total    = 0;
    }
}

static inline uint32_t enterCritical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static inline void exitCritical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

// Append bytes into the ring under the PRIMASK critical section. Returns true
// when something was written (so the caller knows whether to notify).
static bool appendBytes(const uint8_t* data, uint16_t len)
{
    if (data == nullptr || len == 0) {
        return false;
    }

    ensureInit();

    const uint32_t cap = LOGBUFFER_CAPACITY;
    uint32_t n = len;

    // A single write larger than the ring keeps only its last `cap` bytes.
    if (n >= cap) {
        data += (n - cap);
        n = cap;
    }

    const uint32_t primask = enterCritical();

    const uint32_t startIdx = static_cast<uint32_t>(s.total % cap);
    uint32_t first = cap - startIdx;
    if (first > n) {
        first = n;
    }
    std::memcpy(&s.data[startIdx], data, first);
    if (n > first) {
        std::memcpy(&s.data[0], data + first, n - first);
    }

    s.total += n;

    exitCritical(primask);

    return true;
}

void LogBuffer::write(const uint8_t* data, uint16_t len)
{
    if (appendBytes(data, len) && sNotify) {
        sNotify(sNotifyCtx);
    }
}

void LogBuffer::writeNoNotify(const uint8_t* data, uint16_t len)
{
    appendBytes(data, len);
}

const uint8_t* LogBuffer::data()
{
    ensureInit();
    return s.data;
}

uint32_t LogBuffer::size()
{
    ensureInit();
    return (s.total < LOGBUFFER_CAPACITY) ? static_cast<uint32_t>(s.total) : LOGBUFFER_CAPACITY;
}

uint32_t LogBuffer::head()
{
    ensureInit();
    return static_cast<uint32_t>(s.total % LOGBUFFER_CAPACITY);
}

uint32_t LogBuffer::capacity()
{
    return LOGBUFFER_CAPACITY;
}

uint64_t LogBuffer::totalWritten()
{
    ensureInit();
    return s.total;
}

uint32_t LogBuffer::read(uint8_t* dst, uint32_t maxLen, uint64_t* cursor, uint32_t* lost)
{
    if (dst == nullptr || maxLen == 0 || cursor == nullptr) {
        return 0;
    }

    ensureInit();

    const uint32_t cap = LOGBUFFER_CAPACITY;

    const uint32_t primask = enterCritical();

    const uint64_t total  = s.total;
    const uint64_t oldest = (total > cap) ? (total - cap) : 0;

    if (*cursor > total) {
        // Inconsistent (e.g. the buffer cold-reset under us): resync, no loss.
        *cursor = oldest;
    } else if (*cursor < oldest) {
        // The writer overwrote bytes that were never read.
        if (lost != nullptr) {
            *lost += static_cast<uint32_t>(oldest - *cursor);
        }
        *cursor = oldest;
    }

    uint64_t avail = total - *cursor;
    uint32_t n = (avail < maxLen) ? static_cast<uint32_t>(avail) : maxLen;

    if (n > 0) {
        const uint32_t startIdx = static_cast<uint32_t>(*cursor % cap);
        uint32_t first = cap - startIdx;
        if (first > n) {
            first = n;
        }
        std::memcpy(dst, &s.data[startIdx], first);
        if (n > first) {
            std::memcpy(dst + first, &s.data[0], n - first);
        }
        *cursor += n;
    }

    exitCritical(primask);

    return n;
}

void LogBuffer::reset()
{
    const uint32_t primask = enterCritical();
    s.magic    = LOGBUFFER_MAGIC;
    s.capacity = LOGBUFFER_CAPACITY;
    s.total    = 0;
    exitCritical(primask);
}

void LogBuffer::setNotify(NotifyCb cb, void* ctx)
{
    sNotify    = cb;
    sNotifyCtx = ctx;
}
