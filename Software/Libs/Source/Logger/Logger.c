/**
 ******************************************************************************
 * @file    Logger.c
 * @brief   Logger - log to SEMIHOSTING, UART, USB_CDC, RTT, etc.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Logger.h"
#include <stdarg.h>
#include "mpaland/printf/printf.h"

#if !defined(LOG_NO_RTOS)
#include "FreeRTOS.h"
#include "semphr.h"
#endif

// Fallback for environments without CMSIS (e.g. FreeRTOS WIN32 simulator)
#ifndef __STATIC_INLINE
#define __STATIC_INLINE  static inline
#endif

#if (LOG_LEVEL > LOG_LEVEL_NO_LOG)

// Separator between module name and function name in metadata prefix.
// Kept internal - not part of the public API.
#ifndef LOG_MODULE_SEP
#define LOG_MODULE_SEP          "::"
#endif

// Column width for global prefix field in formatted output.
#ifndef LOG_GLOBAL_PRX_WIDTH
#define LOG_GLOBAL_PRX_WIDTH    20
#endif

// Column width reserved for the metadata field (module::func:line).
// If content exceeds this width the fields shift right without truncation.
#ifndef LOG_META_WIDTH
#define LOG_META_WIDTH          36
#endif

// ---------------------------------------------------------------------------
// Level string table - built from LOG_LEVEL_x_STR defines so the format
// is consistent between LOG_* macros (Logger.h) and LOGM() (Logger_fwd.h).
// Override any LOG_LEVEL_x_STR globally to change format everywhere.
// ---------------------------------------------------------------------------

static const char * const kLevelStr[] = {
    "",                    // LOG_LEVEL_NO_LOG = 0
    LOG_LEVEL_ERROR_STR,   // LOG_LEVEL_ERROR   = 1
    LOG_LEVEL_WARNING_STR, // LOG_LEVEL_WARNING = 2
    LOG_LEVEL_INFO_STR,    // LOG_LEVEL_INFO    = 3
    LOG_LEVEL_DEBUG_STR,   // LOG_LEVEL_DEBUG   = 4
};

#define KLEVELSTR_COUNT  ((uint8_t)(sizeof(kLevelStr) / sizeof(kLevelStr[0])))

// ---------------------------------------------------------------------------
// IRQ detection (RTOS only)
// ---------------------------------------------------------------------------

#if !defined(LOG_NO_RTOS)

#if defined(_WIN32) || defined(__linux__)
/**
 * @brief   On hosted platforms (WIN32, Linux) IRQ context detection via
 *          IPSR/PRIMASK is not available - FreeRTOS simulates interrupts
 *          as high-priority threads. Always report non-IRQ context.
 * @retval  Always 0
 */
__STATIC_INLINE uint32_t IRQ_Context(void)
{
    return 0U;
}
#else
/**
 * @brief   Determine whether the CPU is running in a context where the
 *          logger must not block or serialize: interrupt context,
 *          interrupts masked, or the scheduler suspended.
 *
 *          All users of this predicate (Logger_wait / Logger_release /
 *          the emit-buffer selection) share the same semantics: skip the
 *          logger mutex (take AND give, symmetrically) and format in the
 *          dedicated IRQ buffer so a task preempted mid-format in the
 *          shared buffer is not trampled.
 *
 * @retval  1 if in IRQ/masked/scheduler-suspended context,
 *          0 if in normal thread context
 */
__STATIC_INLINE uint32_t IRQ_Context(void)
{
    if (__get_IPSR() != 0U) {
        return 1U;
    }

    switch (xTaskGetSchedulerState()) {
    case taskSCHEDULER_NOT_STARTED:
        // Pre-scheduler boot logging stays on the normal serialized path:
        // with a single execution flow the mutex is always free, so the
        // take below succeeds without ever reaching a blocking wait (and
        // PRIMASK/BASEPRI can be legitimately set this early, so they are
        // deliberately not checked).
        break;
    case taskSCHEDULER_SUSPENDED:
        // Inside a vTaskSuspendAll()/osKernelLock() region (e.g. newlib's
        // __malloc_lock, so any LOG_ reached from inside malloc lands
        // here). A blocking take of the logger mutex - even uncontended,
        // and even with a finite timeout - trips FreeRTOS's
        // configASSERT(!(schedulerSuspended && ticks != 0)) at the top of
        // xQueueSemaphoreTake (queue.c:1591), which reboots the watch.
        // Degrade exactly like IRQ context: unserialized, IRQ buffer, no
        // mutex give.
        return 1U;
    default:
        if ((__get_PRIMASK() != 0U) || (__get_BASEPRI() != 0U)) {
            return 1U;
        }
        break;
    }

    return 0U;
}
#endif // _WIN32 || __linux__

#define LOG_IS_IRQ_MODE()   IRQ_Context()

#else
#define LOG_IS_IRQ_MODE()   (0)
#endif // !LOG_NO_RTOS

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

typedef struct {
#if !defined(LOG_NO_RTOS)
    SemaphoreHandle_t mutHandle;
#if (configSUPPORT_STATIC_ALLOCATION == 1)
    StaticSemaphore_t mutControlBlock;
#endif
#endif
    char               buffer[LOG_BUFFER_SIZE];
    char               irqBuffer[LOG_IRQ_BUFFER_SIZE];
    LoggerWriteFunc_t  pWriteFn;
    LoggerTimeFunc_t   pTimeFn;
    LoggerFilterFunc_t pFilterFn;
    size_t             dumpIndent;  // prefix width cached during hexdump
    bool               isInit;
} Logger_t;

static Logger_t Logger;

// ---------------------------------------------------------------------------
// Cursor helper: advance pointer and shrink remaining space
// ---------------------------------------------------------------------------

/**
 * @brief   Advance write cursor after snprintf call.
 * @param   out       Pointer to current write position (updated in place)
 * @param   rem       Remaining buffer space (updated in place)
 * @param   written   Value returned by snprintf
 */
static inline void cursor_advance(char **out, int *rem, int written)
{
    if (written > 0 && written < *rem) {
        *out += written;
        *rem -= written;
    } else if (written >= *rem) {
        // Advance to end - leave room for '\0'
        *out += *rem - 1;
        *rem  = 1;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Logger_init(LoggerWriteFunc_t pWriteFn)
{
    Logger.pWriteFn = pWriteFn;
    Logger.isInit = true;
}

void Logger_setTimeFunc(LoggerTimeFunc_t pTimeFn)
{
    Logger.pTimeFn = pTimeFn;
}

void Logger_setFilter(LoggerFilterFunc_t pFilterFn)
{
    Logger.pFilterFn = pFilterFn;
}

bool Logger_filter(const char *module, uint8_t level)
{
    if (Logger.pFilterFn) {
        return Logger.pFilterFn(module, level);
    }
    return true;
}

bool Logger_wait(void)
{
    if (!Logger.pWriteFn) {
        return false;
    }

#if !defined(LOG_NO_RTOS)
    if (!Logger.mutHandle) {
#if (configSUPPORT_STATIC_ALLOCATION == 1)
        Logger.mutHandle = xSemaphoreCreateMutexStatic(&Logger.mutControlBlock);
#else
        Logger.mutHandle = xSemaphoreCreateMutex();
#endif
    }

    if (!Logger.mutHandle || LOG_IS_IRQ_MODE()) {
        return true;
    }

    if (xSemaphoreTake(Logger.mutHandle, 1000) != pdPASS) {
        const char *msg = "\nLogger: mutex timeout\n";
        Logger.pWriteFn(msg, strlen(msg));
        return false;
    }
#endif
    return true;
}

void Logger_release(void)
{
#if !defined(LOG_NO_RTOS)
    if (!Logger.mutHandle || LOG_IS_IRQ_MODE()) {
        return;
    }
    xSemaphoreGive(Logger.mutHandle);
#endif
}

// ---------------------------------------------------------------------------
// Core emit - single buffer, single write
// ---------------------------------------------------------------------------

/**
 * @brief   Format prefix part of a log line into provided buffer.
 *          Writes: [timestamp] [global_prx] [-L-] [module[::func]:line] [": "]
 *          depending on with_name / with_meta flags and field values.
 *          Auto-suppress rules:
 *            - global prefix field is skipped if global_prx is empty
 *              (timestamp is still printed when a time function is set)
 *            - level block is skipped if level string is empty
 *            - LOG_MODULE_SEP is skipped if func is empty
 * @param   buf        Destination buffer
 * @param   sz         Buffer size in bytes
 * @param   global_prx Global prefix string
 * @param   module_prx Module name string
 * @param   level      Log level string written as-is, "" to skip level field
 * @param   with_name  If true, write timestamp (when a time function is set) and,
 *                     if global_prx is non-empty, the global_prx field
 * @param   with_meta  If true, write [level +] module[::func]:line + ": "
 * @param   func       Function name, "" to suppress separator
 * @param   line       Line number
 * @retval  Number of characters written (same as snprintf convention)
 */
static size_t Logger_format_prefix(char       *buf,
                                   int         sz,
                                   const char *global_prx,
                                   const char *module_prx,
                                   uint8_t     level,
                                   bool        with_name,
                                   bool        with_meta,
                                   const char *func,
                                   int         line)
{
    char *out = buf;
    int   rem = sz;
    int   n;

    const char *level_str = (level < KLEVELSTR_COUNT) ? kLevelStr[level] : "";

    if (with_name) {
        // Timestamp is independent of the global prefix: print it whenever a
        // time function is set, even when global_prx is empty.
        if (Logger.pTimeFn) {
            n = snprintf(out, rem, "%10lu ", (unsigned long)Logger.pTimeFn());
            cursor_advance(&out, &rem, n);
        }
        // Skip the global prefix field entirely when empty (no stray padding).
        if (global_prx[0] != '\0') {
            n = snprintf(out, rem, "%-*s ", LOG_GLOBAL_PRX_WIDTH, global_prx);
            cursor_advance(&out, &rem, n);
        }
    }

    if (with_meta) {
        // Write level string. Skip entirely if empty (LOG_LEVEL_NO_LOG).
        if (level_str[0] != '\0') {
            n = snprintf(out, rem, "%s ", level_str);
            cursor_advance(&out, &rem, n);
        }

        // Skip separator if func is empty
        if (func[0] != '\0') {
            n = snprintf(out, rem, "%s%s%s:%d",
                         module_prx, LOG_MODULE_SEP, func, line);
        } else {
            n = snprintf(out, rem, "%s:%d",
                         module_prx, line);
        }
        int meta_len = (n < rem) ? n : (rem - 1);
        cursor_advance(&out, &rem, n);

        if (meta_len < LOG_META_WIDTH && rem > 1) {
            int pad = LOG_META_WIDTH - meta_len;
            if (pad >= rem) {
                pad = rem - 1;
            }
            memset(out, ' ', pad);
            out += pad;
            rem -= pad;
        }

        n = snprintf(out, rem, ": ");
        cursor_advance(&out, &rem, n);
    }

    return (size_t)(out - buf);
}

void Logger_emit(const char *global_prx,
                 const char *module_prx,
                 uint8_t     level,
                 bool        with_name,
                 bool        with_meta,
                 const char *func,
                 int         line,
                 const char *fmt,
                 va_list     args)
{
    if (!Logger.pWriteFn) {
        return;
    }

    // Treat NULL the same as "" for all optional string fields
    if (!global_prx)  global_prx  = "";
    if (!module_prx)  module_prx  = "";
    if (!func)        func        = "";

    // Select buffer based on execution context
    bool   irq = (bool)LOG_IS_IRQ_MODE();
    char  *buf = irq ? Logger.irqBuffer : Logger.buffer;
    int    sz  = irq ? LOG_IRQ_BUFFER_SIZE : LOG_BUFFER_SIZE;

    size_t prefix_len = Logger_format_prefix(buf, sz,
                                             global_prx, module_prx, level,
                                             with_name, with_meta,
                                             func, line);
    char *out = buf + prefix_len;
    int   rem = sz  - (int)prefix_len;

    int n = vsnprintf(out, rem, fmt, args);
    cursor_advance(&out, &rem, n);

    Logger.pWriteFn(buf, (size_t)(out - buf));
}

// ---------------------------------------------------------------------------
// Hexdump
// ---------------------------------------------------------------------------

/**
 * @brief   Format one hexdump row into buf.
 * @param   buf          Destination buffer
 * @param   sz           Buffer size
 * @param   data         Pointer to start of full data array
 * @param   offset       Byte offset of this row within data
 * @param   len          Total data length in bytes
 * @param   show_offset  If true, prepend "OOOO: " offset field
 * @param   show_ascii   If true, append "|ASCII|" column
 * @retval  Number of characters written
 */
static size_t Logger_format_hex_row(char          *buf,
                                    int            sz,
                                    const uint8_t *data,
                                    size_t         offset,
                                    size_t         len,
                                    bool           show_offset,
                                    bool           show_ascii)
{
    char *out = buf;
    int   rem = sz;
    int   n;

    // Offset field
    if (show_offset) {
        n = snprintf(out, rem, "%04X: ", (unsigned)offset);
        cursor_advance(&out, &rem, n);
    }

    // Hex bytes
    for (int j = 0; j < 16; j++) {
        if (j == 8) {
            n = snprintf(out, rem, " ");
            cursor_advance(&out, &rem, n);
        }
        if ((offset + j) < len) {
            n = snprintf(out, rem, " %02X", data[offset + j]);
        } else {
            n = snprintf(out, rem, "   ");
        }
        cursor_advance(&out, &rem, n);
    }

    // ASCII column
    if (show_ascii) {
        n = snprintf(out, rem, "  |");
        cursor_advance(&out, &rem, n);

        for (int j = 0; j < 16; j++) {
            char c = ' ';
            if ((offset + j) < len) {
                uint8_t b = data[offset + j];
                c = (b >= 32 && b <= 126) ? (char)b : '.';
            }
            if (rem > 1) {
                *out++ = c;
                rem--;
            }
        }

        n = snprintf(out, rem, "|");
        cursor_advance(&out, &rem, n);
    }

    n = snprintf(out, rem, "\n");
    cursor_advance(&out, &rem, n);

    return (size_t)(out - buf);
}

void Logger_hexdump_safe(const char *global_prx,
                         const char *module_prx,
                         uint8_t     level,
                         bool        with_name,
                         bool        with_meta,
                         const char *func,
                         int         line,
                         const void *data,
                         size_t      len)
{
    if (!data || len == 0) {
        return;
    }

    if (!Logger_filter(global_prx, level) ||
        !Logger_filter(module_prx, level) ||
        !Logger_wait())
    {
        return;
    }

    // Treat NULL the same as "" for all optional string fields
    if (!global_prx)  global_prx  = "";
    if (!module_prx)  module_prx  = "";
    if (!func)        func        = "";

    bool   irq = (bool)LOG_IS_IRQ_MODE();
    char  *buf = irq ? Logger.irqBuffer : Logger.buffer;
    int    sz  = irq ? LOG_IRQ_BUFFER_SIZE : LOG_BUFFER_SIZE;

    const uint8_t *p = (const uint8_t *)data;

    for (size_t offset = 0; offset < len; offset += 16) {

        char  *out = buf;
        int    rem = sz;
        int    n;

        if (offset == 0) {
            // First row: full prefix
            size_t prefix_len = Logger_format_prefix(buf, sz,
                                                     global_prx, module_prx,
                                                     level, with_name, with_meta,
                                                     func, line);
            out += prefix_len;
            rem -= (int)prefix_len;

            // Remember indent for subsequent rows
            Logger.dumpIndent = prefix_len;
        } else {
            // Subsequent rows: spaces to align under first row data
            if (rem > 1) {
                size_t pad = Logger.dumpIndent;
                if ((int)pad >= rem) {
                    pad = (size_t)(rem - 1);
                }
                memset(out, ' ', pad);
                out += pad;
                rem -= (int)pad;
            }
        }

        n = (int)Logger_format_hex_row(out, rem, p, offset, len,
                                       true, LOG_DUMP_ASCII);
        cursor_advance(&out, &rem, n);

        Logger.pWriteFn(buf, (size_t)(out - buf));
    }

    Logger_release();
}

void Logger_hexdump_wp_safe(const char *global_prx,
                            const char *module_prx,
                            uint8_t     level,
                            const void *data,
                            size_t      len)
{
    if (!data || len == 0) {
        return;
    }

    if (!Logger_filter(global_prx, level) ||
        !Logger_filter(module_prx, level) ||
        !Logger_wait())
    {
        return;
    }

    bool   irq = (bool)LOG_IS_IRQ_MODE();
    char  *buf = irq ? Logger.irqBuffer : Logger.buffer;
    int    sz  = irq ? LOG_IRQ_BUFFER_SIZE : LOG_BUFFER_SIZE;

    const uint8_t *p = (const uint8_t *)data;

    for (size_t offset = 0; offset < len; offset += 16) {
        char  *out = buf;
        int    rem = sz;
        int    n   = (int)Logger_format_hex_row(out, rem, p, offset, len,
                                                false, false);
        cursor_advance(&out, &rem, n);
        Logger.pWriteFn(buf, (size_t)(out - buf));
    }

    Logger_release();
}

// ---------------------------------------------------------------------------
// Thread-safe wrappers
// ---------------------------------------------------------------------------

void Logger_message_safe(const char *global_prx,
                         const char *module_prx,
                         uint8_t     level,
                         const char *func,
                         int         line,
                         const char *fmt, ...)
{
    if (!Logger_filter(global_prx, level) ||
        !Logger_filter(module_prx, level) ||
        !Logger_wait())
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    Logger_emit(global_prx, module_prx, level, true, true, func, line, fmt, args);
    va_end(args);

    Logger_release();
}

void Logger_printf_safe(const char *global_prx,
                        const char *module_prx,
                        uint8_t     level,
                        const char *fmt, ...)
{
    if (!Logger_filter(global_prx, level) ||
        !Logger_filter(module_prx, level) ||
        !Logger_wait())
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    Logger_emit(global_prx, module_prx, level, false, false, NULL, 0, fmt, args);
    va_end(args);

    Logger_release();
}

void Logger_module_safe(const char *module_prx,
                        uint8_t     level,
                        const char *func,
                        int         line,
                        const char *fmt, ...)
{
    if (!Logger_filter(module_prx, level) || !Logger_wait()) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    Logger_emit(LOG_GLOBAL_PRX, module_prx, level, true, true, func, line, fmt, args);
    va_end(args);

    Logger_release();
}

void Logger_module_printf_safe(const char *module_prx,
                               uint8_t     level,
                               const char *fmt, ...)
{
    if (!Logger_filter(module_prx, level) || !Logger_wait()) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    Logger_emit(LOG_GLOBAL_PRX, module_prx, level, false, false, NULL, 0, fmt, args);
    va_end(args);

    Logger_release();
}

void Logger_module_hexdump_safe(const char *module_prx,
                                uint8_t     level,
                                const char *func,
                                int         line,
                                const void *data,
                                size_t      len)
{
    Logger_hexdump_safe(LOG_GLOBAL_PRX, module_prx, level,
                        true, true, func, line, data, len);
}

void Logger_module_hexdump_wp_safe(const char *module_prx,
                                   uint8_t     level,
                                   const void *data,
                                   size_t      len)
{
    Logger_hexdump_wp_safe(LOG_GLOBAL_PRX, module_prx, level, data, len);
}

void Logger_raw(const char *data, size_t len)
{
    if (Logger_wait()) {
        Logger.pWriteFn(data, len);
        Logger_release();
    }
}

// ---------------------------------------------------------------------------
// _putchar hook for mpaland printf
// ---------------------------------------------------------------------------

void _putchar(char character)
{
    if (Logger.pWriteFn) {
        Logger.pWriteFn(&character, 1);
    }
}

#endif // (LOG_LEVEL > LOG_LEVEL_NO_LOG)
