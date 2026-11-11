/**
 ******************************************************************************
 * @file    Logger.h
 * @brief   Logger - log to SEMIHOSTING, UART, USB_CDC, RTT, etc.
 ******************************************************************************
 *
 * @usage:
 *
 *   QUICK START
 *   -----------
 *   1) Set global log level via compiler flag or before including Logger.h:
 *          -DLOG_LEVEL=LOG_LEVEL_DEBUG
 *      Available levels (all messages above the selected level are compiled out):
 *          LOG_LEVEL_NO_LOG  = 0  (nothing)
 *          LOG_LEVEL_ERROR   = 1
 *          LOG_LEVEL_WARNING = 2
 *          LOG_LEVEL_INFO    = 3
 *          LOG_LEVEL_DEBUG   = 4
 *
 *   2) In each .c / .cpp file, define module name and level before including:
 *          #define LOG_MODULE_PRX    "MyModule"
 *          #define LOG_MODULE_LEVEL  LOG_LEVEL_DEBUG
 *          #include "Logger.h"
 *      LOG_MODULE_PRX  defaults to the filename if not defined.
 *      LOG_MODULE_LEVEL defaults to LOG_LEVEL_DEBUG if not defined.
 *
 *   3) Initialize once at startup:
 *          LOG_INIT(UART_Send);             // required: output function
 *          LOG_INIT_TIMESTAMP(GetTick);     // optional: timestamp function
 *          LOG_INIT_FILTER(My_filter);      // optional: runtime filter
 *
 *   4) Log from .c / .cpp files:
 *          LOG_ERROR("fault: 0x%08X\n", code);
 *          LOG_WARNING("voltage: %.2f V\n", v);
 *          LOG_INFO("started, ver %d.%d\n", maj, min);
 *          LOG_DEBUG("counter: %d\n", cnt);
 *
 *   5) The logger does NOT append '\n' automatically (same as printf).
 *      Always add '\n' at the end of the format string.
 *
 *
 *   LOGGING FROM HEADER FILES
 *   -------------------------
 *   Including Logger.h in a header breaks all translation units that include
 *   that header, because LOG_MODULE_PRX and LOG_MODULE_LEVEL defined before
 *   the include pollute every file that transitively pulls in the header.
 *
 *   Solution: include Logger_fwd.h instead and use the LOGM_* macro family:
 *
 *          // MyClass.h
 *          #include "Logger_fwd.h"
 *          #define MY_CLASS_LOG  "MyClass"
 *          #define MY_CLASS_LVL  LOG_LEVEL_DEBUG
 *
 *          class MyClass {
 *              inline void foo(int x) {
 *                  LOGM(MY_CLASS_LOG, MY_CLASS_LVL, "x=%d\n", x);
 *              }
 *          };
 *
 *   Logger_fwd.h contains only level constants, function declarations, and
 *   LOGM_* macros - no module defines, no pollution.
 *
 *
 *   MACRO FAMILIES OVERVIEW
 *   -----------------------
 *   For use in .c / .cpp files (require LOG_MODULE_PRX to be set):
 *
 *     LOG_x(fmt, ...)          Full prefix: timestamp + global + level +
 *                              module::func:line
 *     LOG_x_WP(fmt, ...)       No prefix: bare formatted string only
 *     LOG_x_RAW(data, len)     Raw bytes: bypass all formatting and filtering
 *     LOG_x_DUMP(data, len)    Hexdump with full prefix on first row,
 *                              subsequent rows indented to align
 *     LOG_x_DUMP_WP(data, len) Hexdump with no prefix
 *
 *   Where x is: ERROR, WARNING, INFO, DEBUG
 *
 *   For use in header files (module passed explicitly):
 *
 *     LOGM(mod, lvl, fmt, ...)         Full prefix
 *     LOGM_WP(mod, lvl, fmt, ...)      No prefix
 *     LOGM_DUMP(mod, lvl, data, len)   Hexdump with full prefix
 *     LOGM_DUMP_WP(mod, lvl, data, len) Hexdump with no prefix
 *
 *
 *   RUNTIME FILTERING
 *   -----------------
 *   Register a filter callback to suppress messages at runtime:
 *
 *          bool My_filter(const char *module, uint8_t level) {
 *              // suppress DEBUG from "Noisy" module
 *              if (strcmp(module, "Noisy") == 0 && level == LOG_LEVEL_DEBUG)
 *                  return false;
 *              return true;
 *          }
 *          LOG_INIT_FILTER(My_filter);
 *
 *   The filter receives the module name string and numeric level.
 *   Return true to allow, false to suppress.
 *
 *
 *   CUSTOMIZATION
 *   -------------
 *   Level string format (affects both LOG_* and LOGM_* uniformly):
 *          #define LOG_LEVEL_ERROR_STR    "[ERROR]"
 *          #define LOG_LEVEL_WARNING_STR  "[WARN]"
 *          #define LOG_LEVEL_INFO_STR     "[INFO]"
 *          #define LOG_LEVEL_DEBUG_STR    "[DEBUG]"
 *      Defaults: "-E-", "-W-", "-I-", "-D-"
 *
 *   Global prefix (shown in every log line, omitted entirely when empty):
 *          #define LOG_GLOBAL_PRX  "MyApp"    // default: "" (no global prefix)
 *
 *   Buffer sizes:
 *          #define LOG_BUFFER_SIZE      512   // main thread buffer (default)
 *          #define LOG_IRQ_BUFFER_SIZE  128   // IRQ context buffer (default)
 *
 *   Hexdump ASCII column (enabled by default):
 *          #define LOG_DUMP_ASCII  0          // disable ASCII column
 *
 *   RTOS support is active by default. To disable (bare metal):
 *          #define LOG_NO_RTOS
 *   On hosted platforms (WIN32, Linux) IRQ detection is disabled automatically.
 *
 *   To print float values add linker flag:
 *          -u _printf_float
 *
 *
 *   EXAMPLE OUTPUT
 *   --------------
 *        12345 Kernel               -I- MyModule::init:42              : System started
 *        12346 Kernel               -D- MyModule::process:87           : counter: 42
 *        12347 Kernel               -E- MyModule::read:103             : fault: 0xDEADBEEF
 *        12348 Kernel               -D- MyModule::dump:110             : 0000:  01 02 03 04  |....|
 *
 *   Field layout: [timestamp] [global_prx] [level] [module::func:line] : message
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __LOGGER_H_
#define __LOGGER_H_

#include "Logger_fwd.h"

#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Logger version
#define LOG_VER                 5

// Buffer sizes - can be overridden via global defines
#ifndef LOG_BUFFER_SIZE
#define LOG_BUFFER_SIZE         512
#endif

#ifndef LOG_IRQ_BUFFER_SIZE
#define LOG_IRQ_BUFFER_SIZE     128
#endif

#if (LOG_LEVEL > LOG_LEVEL_NO_LOG)

// ---------------------------------------------------------------------------
// Level string defines - override globally to change format for all macros
// ---------------------------------------------------------------------------

#ifndef LOG_LEVEL_ERROR_STR
#define LOG_LEVEL_ERROR_STR     "-E-"
#endif

#ifndef LOG_LEVEL_WARNING_STR
#define LOG_LEVEL_WARNING_STR   "-W-"
#endif

#ifndef LOG_LEVEL_INFO_STR
#define LOG_LEVEL_INFO_STR      "-I-"
#endif

#ifndef LOG_LEVEL_DEBUG_STR
#define LOG_LEVEL_DEBUG_STR     "-D-"
#endif

// ---------------------------------------------------------------------------
// Global and module prefix defaults
// ---------------------------------------------------------------------------

#ifndef LOG_GLOBAL_PRX
#define LOG_GLOBAL_PRX          ""
#endif

#ifndef LOG_MODULE_PRX
#define LOG_MODULE_PRX          __FILENAME__
#endif

#ifndef LOG_MODULE_LEVEL
#define LOG_MODULE_LEVEL        LOG_LEVEL_DEBUG
#endif

// ASCII column in hexdump is enabled by default.
// To disable, define LOG_DUMP_ASCII as 0 in your project:
//   #define LOG_DUMP_ASCII  0
#ifndef LOG_DUMP_ASCII
#define LOG_DUMP_ASCII          1
#endif

// ---------------------------------------------------------------------------
// Callback types
// ---------------------------------------------------------------------------

/**
 * @brief   Callback function type for writing data to output interface.
 * @param   data Pointer to char buffer
 * @param   len  Number of characters to write
 */
typedef void (*LoggerWriteFunc_t)(const char *data, size_t len);

/**
 * @brief   Callback function type for getting system timestamp in milliseconds.
 * @retval  Current system time in milliseconds
 */
typedef uint32_t (*LoggerTimeFunc_t)(void);

/**
 * @brief   Callback function type for filtering log messages.
 *          Convention: NULL or "" for module means no filtering on module.
 *          LOG_LEVEL_NO_LOG (0) for level means no filtering on level.
 * @param   module Module name, or NULL/"" to skip module filtering
 * @param   level  Numeric log level (LOG_LEVEL_ERROR..LOG_LEVEL_DEBUG),
 *                 LOG_LEVEL_NO_LOG to skip level filtering
 * @retval  true to allow the message, false to suppress it
 */
typedef bool (*LoggerFilterFunc_t)(const char *module, uint8_t level);

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * @brief   Initialize the logger with output function.
 * @param   pWriteFn Pointer to external write callback
 */
void Logger_init(LoggerWriteFunc_t pWriteFn);

/**
 * @brief   Set timestamp function for the logger.
 * @param   pTimeFn Pointer to function returning system time in milliseconds
 */
void Logger_setTimeFunc(LoggerTimeFunc_t pTimeFn);

/**
 * @brief   Set filter function for the logger.
 * @param   pFilterFn Pointer to filter callback
 */
void Logger_setFilter(LoggerFilterFunc_t pFilterFn);

/**
 * @brief   Core emit function. Formats a complete log line into a single buffer
 *          and sends it via one call to the write callback.
 *          All public logging functions funnel through here.
 *          Does NOT append '\n' automatically - include it in fmt if needed.
 * @param   global_prx  Global system prefix string, NULL or "" treated as ""
 * @param   module_prx  Module name string, NULL or "" treated as ""
 * @param   level       Numeric log level, resolved to string via kLevelStr table
 * @param   with_name   If true, prepend timestamp (if set) + global_prx
 * @param   with_meta   If true, prepend level + module_prx::func:line
 * @param   func        Function name, NULL or "" treated as ""
 * @param   line        Line number
 * @param   fmt         printf-style format string, must not be NULL
 * @param   args        Variable argument list
 */
void Logger_emit(const char *global_prx,
                 const char *module_prx,
                 uint8_t     level,
                 bool        with_name,
                 bool        with_meta,
                 const char *func,
                 int         line,
                 const char *fmt,
                 va_list     args);

/**
 * @brief   Emit a log message with full metadata prefix (thread-safe).
 *          Does NOT append '\n' automatically - include it in fmt if needed.
 * @param   global_prx  Global system prefix, NULL or "" treated as ""
 * @param   module_prx  Module name, NULL or "" treated as ""
 * @param   level       Numeric log level (LOG_LEVEL_ERROR..LOG_LEVEL_DEBUG),
 *                      LOG_LEVEL_NO_LOG to skip level field and level filtering
 * @param   func        Function name, NULL or "" treated as ""
 * @param   line        Line number
 * @param   fmt         printf-style format string, must not be NULL
 * @param   ...         Format arguments
 */
void Logger_message_safe(const char *global_prx,
                         const char *module_prx,
                         uint8_t     level,
                         const char *func,
                         int         line,
                         const char *fmt, ...);

/**
 * @brief   Emit a formatted string without any metadata prefix (thread-safe).
 *          Does NOT append '\n' automatically - include it in fmt if needed.
 * @param   global_prx  Global prefix, NULL or "" to skip module filtering
 * @param   module_prx  Module name, NULL or "" to skip module filtering
 * @param   level       Numeric log level, LOG_LEVEL_NO_LOG to skip level filtering
 * @param   fmt         printf-style format string, must not be NULL
 * @param   ...         Format arguments
 */
void Logger_printf_safe(const char *global_prx,
                        const char *module_prx,
                        uint8_t     level,
                        const char *fmt, ...);

/**
 * @brief   Send raw characters directly to the output interface (thread-safe).
 *          Bypasses all formatting and filtering. Behavior mirrors printf:
 *          no '\n' is appended automatically.
 * @param   data Pointer to char buffer, must not be NULL
 * @param   len  Number of characters to send
 */
void Logger_raw(const char *data, size_t len);

/**
 * @brief   Print hexdump with full metadata prefix on the first row,
 *          subsequent rows indented to align with first row data (thread-safe).
 *          Row format (with LOG_DUMP_ASCII): "OOOO:  XX ... XX  |ASCII|\n"
 *          Row format (without LOG_DUMP_ASCII): "OOOO:  XX ... XX\n"
 * @param   global_prx  Global prefix, NULL or "" treated as ""
 * @param   module_prx  Module name, NULL or "" treated as ""
 * @param   level       Numeric log level, LOG_LEVEL_NO_LOG to skip level field
 * @param   with_name   If true, prepend timestamp + global_prx on first row
 * @param   with_meta   If true, prepend level + module::func:line on first row
 * @param   func        Function name, NULL or "" treated as ""
 * @param   line        Line number
 * @param   data        Pointer to data buffer, must not be NULL
 * @param   len         Number of bytes to dump
 */
void Logger_hexdump_safe(const char *global_prx,
                         const char *module_prx,
                         uint8_t     level,
                         bool        with_name,
                         bool        with_meta,
                         const char *func,
                         int         line,
                         const void *data,
                         size_t      len);

/**
 * @brief   Print hexdump without any metadata prefix (thread-safe).
 *          Each row: "OOOO:  XX ... XX  [|ASCII|]\n"
 *          Filtered by global_prx and module_prx/level same as other log calls.
 * @param   global_prx  Global prefix, NULL or "" to skip module filtering
 * @param   module_prx  Module name, NULL or "" to skip module filtering
 * @param   level       Numeric log level, LOG_LEVEL_NO_LOG to skip level filtering
 * @param   data        Pointer to data buffer, must not be NULL
 * @param   len         Number of bytes to dump
 */
void Logger_hexdump_wp_safe(const char *global_prx,
                            const char *module_prx,
                            uint8_t     level,
                            const void *data,
                            size_t      len);

/**
 * @brief   Check whether a message should be logged based on the filter callback.
 * @param   module Module name, or NULL/"" to skip module filtering
 * @param   level  Numeric log level, LOG_LEVEL_NO_LOG to skip level filtering
 * @retval  true if message should be logged, false if suppressed
 */
bool Logger_filter(const char *module, uint8_t level);

/**
 * @brief   Acquire the logger mutex (no-op when LOG_NO_RTOS is defined).
 * @retval  true if mutex acquired or RTOS not used, false on timeout
 */
bool Logger_wait(void);

/**
 * @brief   Release the logger mutex (no-op when LOG_NO_RTOS is defined).
 */
void Logger_release(void);

// ---------------------------------------------------------------------------
// Initialization macros
// ---------------------------------------------------------------------------

#define LOG_INIT(X)             Logger_init(X)
#define LOG_INIT_TIMESTAMP(X)   Logger_setTimeFunc(X)
#define LOG_INIT_FILTER(X)      Logger_setFilter(X)

// ---------------------------------------------------------------------------
// Base macros - pass numeric level, level string resolved in Logger.c
// ---------------------------------------------------------------------------

#define LOG(LEVEL, FMT, ...) \
        Logger_message_safe(LOG_GLOBAL_PRX, LOG_MODULE_PRX, \
                            LOG_LEVEL_##LEVEL, \
                            __FUNCTION_NAME__, __LINE__, \
                            FMT, ##__VA_ARGS__)

#define LOG_WP(LEVEL, ...) \
        Logger_printf_safe(LOG_GLOBAL_PRX, LOG_MODULE_PRX, \
                           LOG_LEVEL_##LEVEL, __VA_ARGS__)

#define LOG_RAW(DATA, LEN) \
        Logger_raw(DATA, LEN)

#define LOG_DUMP(LEVEL, DATA, LEN) \
        Logger_hexdump_safe(LOG_GLOBAL_PRX, LOG_MODULE_PRX, \
                            LOG_LEVEL_##LEVEL, \
                            true, true, __FUNCTION_NAME__, __LINE__, \
                            DATA, LEN)

#define LOG_DUMP_WP(LEVEL, DATA, LEN) \
        Logger_hexdump_wp_safe(LOG_GLOBAL_PRX, LOG_MODULE_PRX, \
                               LOG_LEVEL_##LEVEL, \
                               DATA, LEN)

#else // LOG_LEVEL == LOG_LEVEL_NO_LOG

#define Logger_init(...)            do { } while(0)
#define Logger_setTimeFunc(...)     do { } while(0)
#define Logger_setFilter(...)       do { } while(0)
#define Logger_message_safe(...)        do { } while(0)
#define Logger_printf_safe(...)         do { } while(0)
#define Logger_module_safe(...)             do { } while(0)
#define Logger_module_printf_safe(...)      do { } while(0)
#define Logger_module_hexdump_safe(...)     do { } while(0)
#define Logger_module_hexdump_wp_safe(...)  do { } while(0)
#define Logger_raw(...)                 do { } while(0)
#define Logger_wait(...)                do { } while(0)
#define Logger_release(...)             do { } while(0)

#define LOG_INIT(X)                 do { } while(0)
#define LOG_INIT_TIMESTAMP(X)       do { } while(0)
#define LOG_INIT_FILTER(X)          do { } while(0)
#define LOG(...)                    do { } while(0)
#define LOG_WP(...)                 do { } while(0)
#define LOG_RAW(...)                do { } while(0)
#define LOG_DUMP(...)               do { } while(0)
#define LOG_DUMP_WP(...)            do { } while(0)

#endif // (LOG_LEVEL > LOG_LEVEL_NO_LOG)


// ---------------------------------------------------------------------------
// User-facing macros per log level
// ---------------------------------------------------------------------------

#if (LOG_LEVEL >= LOG_LEVEL_DEBUG && LOG_MODULE_LEVEL >= LOG_LEVEL_DEBUG)
#define LOG_DEBUG(FMT, ...)             LOG(DEBUG,   FMT, ##__VA_ARGS__)
#define LOG_DEBUG_WP(...)               LOG_WP(DEBUG,   __VA_ARGS__)
#define LOG_DEBUG_RAW(DATA, LEN)        LOG_RAW(DATA, LEN)
#define LOG_DEBUG_DUMP(DATA, LEN)       LOG_DUMP(DEBUG,   DATA, LEN)
#define LOG_DEBUG_DUMP_WP(DATA, LEN)    LOG_DUMP_WP(DEBUG,   DATA, LEN)
#else
#define LOG_DEBUG(...)                  do { } while(0)
#define LOG_DEBUG_WP(...)               do { } while(0)
#define LOG_DEBUG_RAW(...)              do { } while(0)
#define LOG_DEBUG_DUMP(...)             do { } while(0)
#define LOG_DEBUG_DUMP_WP(...)          do { } while(0)
#endif

#if (LOG_LEVEL >= LOG_LEVEL_INFO && LOG_MODULE_LEVEL >= LOG_LEVEL_INFO)
#define LOG_INFO(FMT, ...)              LOG(INFO,    FMT, ##__VA_ARGS__)
#define LOG_INFO_WP(...)                LOG_WP(INFO,    __VA_ARGS__)
#define LOG_INFO_RAW(DATA, LEN)         LOG_RAW(DATA, LEN)
#define LOG_INFO_DUMP(DATA, LEN)        LOG_DUMP(INFO,    DATA, LEN)
#define LOG_INFO_DUMP_WP(DATA, LEN)     LOG_DUMP_WP(INFO,    DATA, LEN)
#else
#define LOG_INFO(...)                   do { } while(0)
#define LOG_INFO_WP(...)                do { } while(0)
#define LOG_INFO_RAW(...)               do { } while(0)
#define LOG_INFO_DUMP(...)              do { } while(0)
#define LOG_INFO_DUMP_WP(...)           do { } while(0)
#endif

#if (LOG_LEVEL >= LOG_LEVEL_WARNING && LOG_MODULE_LEVEL >= LOG_LEVEL_WARNING)
#define LOG_WARNING(FMT, ...)           LOG(WARNING, FMT, ##__VA_ARGS__)
#define LOG_WARNING_WP(...)             LOG_WP(WARNING, __VA_ARGS__)
#define LOG_WARNING_RAW(DATA, LEN)      LOG_RAW(DATA, LEN)
#define LOG_WARNING_DUMP(DATA, LEN)     LOG_DUMP(WARNING, DATA, LEN)
#define LOG_WARNING_DUMP_WP(DATA, LEN)  LOG_DUMP_WP(WARNING, DATA, LEN)
#else
#define LOG_WARNING(...)                do { } while(0)
#define LOG_WARNING_WP(...)             do { } while(0)
#define LOG_WARNING_RAW(...)            do { } while(0)
#define LOG_WARNING_DUMP(...)           do { } while(0)
#define LOG_WARNING_DUMP_WP(...)        do { } while(0)
#endif

#if (LOG_LEVEL >= LOG_LEVEL_ERROR && LOG_MODULE_LEVEL >= LOG_LEVEL_ERROR)
#define LOG_ERROR(FMT, ...)             LOG(ERROR,   FMT, ##__VA_ARGS__)
#define LOG_ERROR_WP(...)               LOG_WP(ERROR,   __VA_ARGS__)
#define LOG_ERROR_RAW(DATA, LEN)        LOG_RAW(DATA, LEN)
#define LOG_ERROR_DUMP(DATA, LEN)       LOG_DUMP(ERROR,   DATA, LEN)
#define LOG_ERROR_DUMP_WP(DATA, LEN)    LOG_DUMP_WP(ERROR,   DATA, LEN)
#else
#define LOG_ERROR(...)                  do { } while(0)
#define LOG_ERROR_WP(...)               do { } while(0)
#define LOG_ERROR_RAW(...)              do { } while(0)
#define LOG_ERROR_DUMP(...)             do { } while(0)
#define LOG_ERROR_DUMP_WP(...)          do { } while(0)
#endif

#ifdef __cplusplus
}
#endif

#endif // __LOGGER_H_
