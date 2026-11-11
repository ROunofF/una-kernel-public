/**
 ******************************************************************************
 * @file    LogBuffer.hpp
 * @brief   Self-contained no-init ring buffer for system logging.
 *
 * Standalone circular buffer with zero dependencies (no FS, no Crash, no RTOS).
 * Storage and write cursor live in a dedicated no-init RAM section, so the
 * buffer survives a warm reset and is usable from the very first LOG_* call,
 * before any explicit initialization runs.
 *
 * The buffer is free-running: it never clears on a normal reboot, only on a
 * cold boot (when the no-init magic is invalid). Readers (Crash, SystemLogger)
 * track their own cursors and linearize the ring using head()/size().
 *
 * Linearization rule (oldest -> newest), where cap == capacity():
 *   size = size();
 *   if (size < cap)   valid = data[0 .. size)            // head == size
 *   else              valid = data[head .. cap) ++ data[0 .. head)
 *   oldest_index = (size < cap) ? 0 : head;
 ******************************************************************************
 */

#ifndef LOG_BUFFER_HPP
#define LOG_BUFFER_HPP

#include <cstdint>
#include <cstddef>

/** Capacity of the log ring in bytes. */
#define LOGBUFFER_CAPACITY  (64u * 1024u)

/**
 * @brief No-init system log ring buffer (static, single instance).
 *
 * All methods are static and operate on a no-init POD storage placed in the
 * .log_buffer section. There is no instance and no constructor, so the buffer
 * is safe to use before C++ static initialization (t = 0).
 */
class LogBuffer {
public:
    /**
     * @brief Append bytes to the ring (sink for LOG_*). IRQ-safe.
     *
     * Overwrites the oldest data once full. Safe to call from any context,
     * including with interrupts disabled (uses a short PRIMASK critical section).
     *
     * @param[in] data Pointer to bytes to append.
     * @param[in] len  Number of bytes.
     */
    static void write(const uint8_t* data, uint16_t len);

    /**
     * @brief Append bytes like write(), but without firing the notify callback.
     *
     * For use from a fault/crash path: the append is the same bounded PRIMASK
     * critical section, but the notify (which would wake a consumer thread via
     * the RTOS) is skipped, so nothing touches scheduler state in a context where
     * that is unsafe and pointless (a reset is imminent). The bytes still survive
     * the reset and are picked up by the next reader.
     *
     * @param[in] data Pointer to bytes to append.
     * @param[in] len  Number of bytes.
     */
    static void writeNoNotify(const uint8_t* data, uint16_t len);

    /** @return Pointer to the raw ring storage (base of the data region). */
    static const uint8_t* data();

    /** @return Number of valid bytes, 0..capacity() (saturates when full). */
    static uint32_t size();

    /** @return Index of the next write position, [0, capacity()). */
    static uint32_t head();

    /** @return Ring capacity in bytes (== LOGBUFFER_CAPACITY). */
    static uint32_t capacity();

    /**
     * @brief Monotonic count of bytes ever written (the source of truth).
     *
     * Never wraps within the lifetime of a power session. A drain consumer keeps
     * its own last-drained value; `totalWritten() - lastDrained` is the number of
     * new bytes, and if that exceeds capacity() the consumer lagged and the
     * difference beyond capacity() was overwritten (lost).
     */
    static uint64_t totalWritten();

    /**
     * @brief Atomically copy out up to @p maxLen bytes starting at *cursor.
     *
     * Safe against a concurrent writer: the copy runs under the same critical
     * section as write(), so the returned chunk is always a consistent snapshot
     * (never torn by an overwrite mid-read). Advances *cursor by the bytes
     * copied. If the writer has already overwritten not-yet-read bytes, *cursor
     * is fast-forwarded to the oldest still-available byte and *lost (when
     * non-null) is increased by the number of bytes skipped.
     *
     * This is the correct path for a streaming drain consumer; prefer it over
     * the raw data()/head()/size() accessors, which are only safe when no write
     * can race the read. Keep @p maxLen modest (e.g. <= 512) to bound the time
     * interrupts are disabled during the copy.
     *
     * @param[out]    dst    Destination buffer (>= maxLen bytes).
     * @param[in]     maxLen Maximum bytes to copy this call.
     * @param[in,out] cursor Absolute read position; advanced by the return value.
     * @param[in,out] lost   Optional; incremented by bytes lost to overwrite.
     * @return Bytes copied into @p dst (0 when caught up to the writer).
     */
    static uint32_t read(uint8_t* dst, uint32_t maxLen, uint64_t* cursor, uint32_t* lost);

    /** @brief Discard all content (cold-init / manual clear). */
    static void reset();

    /** Notification callback type fired after each write (see setNotify). */
    typedef void (*NotifyCb)(void* ctx);

    /**
     * @brief Register a callback invoked after every write (outside the critical
     *        section). Lets a consumer wake on "not empty" instead of polling.
     *
     * The callback runs in the writer's context, which may be an ISR, so it must
     * be ISR-safe. Keeps LogBuffer dependency-free: it only calls a function
     * pointer; the RTOS signalling lives in the consumer. Pass nullptr to clear.
     */
    static void setNotify(NotifyCb cb, void* ctx);
};

#endif /* LOG_BUFFER_HPP */
