/**
 ******************************************************************************
 * @file    GuiLogger.hpp
 * @brief   Logger component for GUI.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __GUI_LOGGER_HPP
#define __GUI_LOGGER_HPP



#if defined(SIMULATOR)

#include <touchgfx/Utils.hpp>
#include "Windows.h"



// Log level
#define LOG_LEVEL_NO_LOG        0
#define LOG_LEVEL_ERROR         1
#define LOG_LEVEL_WARNING       2
#define LOG_LEVEL_INFO          3
#define LOG_LEVEL_DEBUG         4

#if !defined(LOG_LEVEL)
#define LOG_LEVEL               LOG_LEVEL_DEBUG
#endif

#if (LOG_LEVEL > LOG_LEVEL_NO_LOG)

#define Logger_printf    touchgfx_printf

#define Logger_printTimestamp() do { Logger_printf("%10lu ", (uint32_t)GetTickCount64()); } while(0)



#define Logger_raw() do{ } while (0)

#define  Logger_printDumpBytes(pData, len)                      \
{                                                               \
    uint32_t i = 0;                                             \
    uint8_t *p = ( uint8_t * )pData;                            \
    if (len > 0 && pData != NULL){                              \
        for (i = 0; i < len;){                                  \
            if ((i & 0x0f) == 0){                               \
                Logger_printf("%s   ", (i == 0) ? "" : "\n");   \
            } else if ((i & 0x07) == 0 && i != 0){              \
                Logger_printf(" ");                             \
            }                                                   \
            Logger_printf(" %02X", p[i++]);                     \
        }                                                       \
        Logger_printf("\n");                                    \
    }                                                           \
}                                                               \


#ifndef LOG_MODULE_PRX
#define LOG_MODULE_PRX          "GUI"
#endif

#ifndef LOG_MODULE_LEVEL
#define LOG_MODULE_LEVEL        LOG_LEVEL_DEBUG
#endif

/* Get file name from __FILE__ without full path */
#ifndef __FILE_NAME__
#define __FILE_NAME__       (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#endif

#define LOG_FORMAT_STR(LEVEL, FMT, ...) Logger_printf("-%s- %s::%s:%d  " FMT, #LEVEL, LOG_MODULE_PRX, __FUNCTION__, __LINE__, ##__VA_ARGS__);


#define LOG(LEVEL, FMT, ...)            do { Logger_printTimestamp(); LOG_FORMAT_STR(LEVEL, FMT, ##__VA_ARGS__); } while(0)

#define LOG_WP(...)                     Logger_printf(__VA_ARGS__)

#define LOG_DUMP(DATA,LEN)              Logger_printDumpBytes(DATA, LEN)

#endif // (LOG_LEVEL > LOG_LEVEL_NO_LOG)

// User MACRO

#if (LOG_LEVEL >= LOG_LEVEL_DEBUG && LOG_MODULE_LEVEL >= LOG_LEVEL_DEBUG)
#define LOG_DEBUG(FMT, ...)             LOG(D, FMT, ##__VA_ARGS__)
#define LOG_DEBUG_WP(...)               LOG_WP(__VA_ARGS__)
#define LOG_DEBUG_DUMP(DATA, LEN)       LOG_DUMP(DATA,LEN)
#else
#define LOG_DEBUG(...)                  do { } while(0)
#define LOG_DEBUG_WP(...)               do { } while(0)
#define LOG_DEBUG_DUMP(...)             do { } while(0)
#endif

#if (LOG_LEVEL >= LOG_LEVEL_INFO && LOG_MODULE_LEVEL >= LOG_LEVEL_INFO)
#define LOG_INFO(FMT, ...)              LOG(I, FMT, ##__VA_ARGS__)
#define LOG_INFO_WP(...)                LOG_WP(__VA_ARGS__)
#define LOG_INFO_DUMP(DATA, LEN)        LOG_DUMP(DATA,LEN)
#else
#define LOG_INFO(...)                   do { } while(0)
#define LOG_INFO_WP(...)                do { } while(0)
#define LOG_INFO_DUMP(...)              do { } while(0)
#endif

#if (LOG_LEVEL >= LOG_LEVEL_WARNING && LOG_MODULE_LEVEL >= LOG_LEVEL_WARNING)
#define LOG_WARNING(FMT, ...)           LOG(W, FMT, ##__VA_ARGS__)
#define LOG_WARNING_WP(...)             LOG_WP(__VA_ARGS__)
#define LOG_WARNING_DUMP(DATA, LEN)     LOG_DUMP(DATA,LEN)
#else
#define LOG_WARNING(...)                do { } while(0)
#define LOG_WARNING_WP(...)             do { } while(0)
#define LOG_WARNING_DUMP(...)           do { } while(0)
#endif

#if (LOG_LEVEL >= LOG_LEVEL_ERROR && LOG_MODULE_LEVEL >= LOG_LEVEL_ERROR)
#define LOG_ERROR(FMT, ...)             LOG(E, FMT, ##__VA_ARGS__)
#define LOG_ERROR_WP(...)               LOG_WP(__VA_ARGS__)
#define LOG_ERROR_DUMP(DATA, LEN)       LOG_DUMP(DATA,LEN)
#else
#define LOG_ERROR(...)                  do { } while(0)
#define LOG_ERROR_WP(...)               do { } while(0)
#define LOG_ERROR_DUMP(...)             do { } while(0)

#endif


#elif defined(STM32U595xx) || defined(STM32U5A5xx)
#include "Logger.h"
#else
#warning "Platform not defined!"
#endif

#endif /* __GUI_LOGGER_HPP */
