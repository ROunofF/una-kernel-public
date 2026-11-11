/**
 ******************************************************************************
 * @file    Logger_fwd.h
 * @brief   Logger forward declarations - safe to include in header files.
 *          Provides the LOGM_* macro family for logging from inline functions
 *          and class methods defined in headers, without polluting the
 *          including translation unit with module-level defines.
 *
 * @usage:
 *
 *   WHY THIS FILE EXISTS
 *   --------------------
 *   Logger.h relies on LOG_MODULE_PRX and LOG_MODULE_LEVEL being defined
 *   before the include in each .c/.cpp file. If Logger.h is included in a
 *   header, those defines leak into every translation unit that pulls in
 *   that header, breaking per-module settings.
 *
 *   Logger_fwd.h contains only level constants, compiler helpers, function
 *   forward declarations, and the LOGM_* macros. It is safe to include in
 *   any header file at any point - no module defines, no side effects.
 *
 *
 *   LOGM_* MACRO FAMILY
 *   --------------------
 *   All four macros pass module name and level explicitly. The compile-time
 *   level gate (LOG_LEVEL) still applies - calls below the global threshold
 *   are eliminated entirely by the preprocessor.
 *
 *     LOGM(mod, lvl, fmt, ...)
 *         Full prefix: timestamp + global_prx + level + module::func:line
 *         Equivalent to LOG_x() from Logger.h
 *
 *     LOGM_WP(mod, lvl, fmt, ...)
 *         No prefix: bare formatted string only
 *         Equivalent to LOG_x_WP() from Logger.h
 *
 *     LOGM_DUMP(mod, lvl, data, len)
 *         Hexdump with full prefix on first row, subsequent rows indented
 *         Equivalent to LOG_x_DUMP() from Logger.h
 *
 *     LOGM_DUMP_WP(mod, lvl, data, len)
 *         Hexdump with no prefix
 *         Equivalent to LOG_x_DUMP_WP() from Logger.h
 *
 *
 *   USAGE IN A HEADER FILE
 *   ----------------------
 *   @code
 *   // MyClass.h
 *   #include "Logger_fwd.h"
 *
 *   #define MY_CLASS_LOG  "MyClass"
 *   #define MY_CLASS_LVL  LOG_LEVEL_DEBUG
 *
 *   class MyClass {
 *   public:
 *       inline void process(int x) {
 *           LOGM(MY_CLASS_LOG, MY_CLASS_LVL, "x=%d\n", x);
 *       }
 *       inline void dump(const uint8_t *buf, size_t len) {
 *           LOGM_DUMP(MY_CLASS_LOG, MY_CLASS_LVL, buf, len);
 *       }
 *   };
 *   @endcode
 *
 *
 *   USAGE IN A .c / .cpp FILE (unchanged, Logger.h as before)
 *   ----------------------------------------------------------
 *   @code
 *   // MyModule.c
 *   #define LOG_MODULE_PRX    "MyModule"
 *   #define LOG_MODULE_LEVEL  LOG_LEVEL_DEBUG
 *   #include "Logger.h"
 *   #include "MyClass.h"      // safe - Logger_fwd.h has no side effects
 *
 *   void MyModule_run(void) {
 *       LOG_INFO("running\n");
 *   }
 *   @endcode
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __LOGGER_FWD_H_
#define __LOGGER_FWD_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Log level definitions (shared with Logger.h)
// ---------------------------------------------------------------------------

#define LOG_LEVEL_NO_LOG        0
#define LOG_LEVEL_ERROR         1
#define LOG_LEVEL_WARNING       2
#define LOG_LEVEL_INFO          3
#define LOG_LEVEL_DEBUG         4

#if !defined(LOG_LEVEL)
#define LOG_LEVEL               LOG_LEVEL_NO_LOG
#endif

// ---------------------------------------------------------------------------
// Compiler portability helpers
// ---------------------------------------------------------------------------

#ifndef __FILENAME__
#if defined(_WIN32) || defined(_WIN64)
    #define __FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#else
    #define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif
#endif

#ifndef __FUNCTION_NAME__
#if defined(__GNUC__) || defined(__clang__)
    #define __FUNCTION_NAME__ __func__
#elif defined(_MSC_VER)
    #define __FUNCTION_NAME__ __FUNCTION__
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
    #define __FUNCTION_NAME__ __func__
#else
    #define __FUNCTION_NAME__ "unknown"
#endif
#endif

// ---------------------------------------------------------------------------
// Forward declaration of the module-aware logging function
// ---------------------------------------------------------------------------

/**
 * @brief   Emit a log message with module name and numeric level (thread-safe).
 *          Level string is resolved internally from kLevelStr table using
 *          the LOG_LEVEL_x_STR defines, so format is consistent with LOG_*
 *          macros from Logger.h.
 *          Does NOT append '\n' automatically - include it in fmt if needed.
 * @param   module_prx  Module name string, NULL or "" treated as ""
 * @param   level       Numeric log level (LOG_LEVEL_ERROR .. LOG_LEVEL_DEBUG)
 * @param   func        Function name, NULL or "" treated as ""
 * @param   line        Line number
 * @param   fmt         printf-style format string, must not be NULL
 * @param   ...         Format arguments
 */
void Logger_module_safe(const char *module_prx,
                        uint8_t     level,
                        const char *func,
                        int         line,
                        const char *fmt, ...);

/**
 * @brief   Emit a formatted string without metadata prefix, with module
 *          filtering (thread-safe). Mirrors LOG_x_WP behavior.
 *          Does NOT append '\n' automatically - include it in fmt if needed.
 * @param   module_prx  Module name string, NULL or "" to skip module filtering
 * @param   level       Numeric log level, LOG_LEVEL_NO_LOG to skip level filtering
 * @param   fmt         printf-style format string, must not be NULL
 * @param   ...         Format arguments
 */
void Logger_module_printf_safe(const char *module_prx,
                               uint8_t     level,
                               const char *fmt, ...);

/**
 * @brief   Print hexdump with full metadata prefix, with module filtering
 *          (thread-safe). Mirrors LOG_x_DUMP behavior.
 *          First row has full prefix, subsequent rows are indented to align.
 * @param   module_prx  Module name string, NULL or "" treated as ""
 * @param   level       Numeric log level, LOG_LEVEL_NO_LOG to skip level field
 * @param   func        Function name, NULL or "" treated as ""
 * @param   line        Line number
 * @param   data        Pointer to data buffer, must not be NULL
 * @param   len         Number of bytes to dump
 */
void Logger_module_hexdump_safe(const char *module_prx,
                                uint8_t     level,
                                const char *func,
                                int         line,
                                const void *data,
                                size_t      len);

/**
 * @brief   Print hexdump without metadata prefix, with module filtering
 *          (thread-safe). Mirrors LOG_x_DUMP_WP behavior.
 * @param   module_prx  Module name string, NULL or "" to skip module filtering
 * @param   level       Numeric log level, LOG_LEVEL_NO_LOG to skip level filtering
 * @param   data        Pointer to data buffer, must not be NULL
 * @param   len         Number of bytes to dump
 */
void Logger_module_hexdump_wp_safe(const char *module_prx,
                                   uint8_t     level,
                                   const void *data,
                                   size_t      len);

// ---------------------------------------------------------------------------
// LOGM macros - use in header files instead of LOG_* macros.
// Compile-time level gate: eliminated entirely when LOG_LEVEL is too low.
// ---------------------------------------------------------------------------

#if (LOG_LEVEL > LOG_LEVEL_NO_LOG)

#define LOGM(MODULE, LEVEL, FMT, ...) \
    do { \
        if ((uint8_t)(LEVEL) <= (uint8_t)(LOG_LEVEL)) { \
            Logger_module_safe(MODULE, (uint8_t)(LEVEL), \
                               __FUNCTION_NAME__, __LINE__, \
                               FMT, ##__VA_ARGS__); \
        } \
    } while (0)

#define LOGM_WP(MODULE, LEVEL, FMT, ...) \
    do { \
        if ((uint8_t)(LEVEL) <= (uint8_t)(LOG_LEVEL)) { \
            Logger_module_printf_safe(MODULE, (uint8_t)(LEVEL), \
                                      FMT, ##__VA_ARGS__); \
        } \
    } while (0)

#define LOGM_DUMP(MODULE, LEVEL, DATA, LEN) \
    do { \
        if ((uint8_t)(LEVEL) <= (uint8_t)(LOG_LEVEL)) { \
            Logger_module_hexdump_safe(MODULE, (uint8_t)(LEVEL), \
                                       __FUNCTION_NAME__, __LINE__, \
                                       DATA, LEN); \
        } \
    } while (0)

#define LOGM_DUMP_WP(MODULE, LEVEL, DATA, LEN) \
    do { \
        if ((uint8_t)(LEVEL) <= (uint8_t)(LOG_LEVEL)) { \
            Logger_module_hexdump_wp_safe(MODULE, (uint8_t)(LEVEL), \
                                          DATA, LEN); \
        } \
    } while (0)

#else

#define LOGM(MODULE, LEVEL, FMT, ...)           do { } while (0)
#define LOGM_WP(MODULE, LEVEL, FMT, ...)        do { } while (0)
#define LOGM_DUMP(MODULE, LEVEL, DATA, LEN)     do { } while (0)
#define LOGM_DUMP_WP(MODULE, LEVEL, DATA, LEN)  do { } while (0)

#endif // (LOG_LEVEL > LOG_LEVEL_NO_LOG)

#ifdef __cplusplus
}
#endif

#endif // __LOGGER_FWD_H_
