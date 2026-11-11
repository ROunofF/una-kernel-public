/**
 ******************************************************************************
 * @file    PixArtPort.cpp
 * @brief   The functions the PixArt PPG library calls into the firmware for
 *          (see pah_port.h).
 ******************************************************************************
 */

#include "Logger.h"

#include <cstdarg>

extern "C" {
#include "pah_port.h"
}

extern "C" void pah_port_log(uint8_t level, bool with_meta, const char *module,
                             const char *func, int line, const char *fmt, ...)
{
#if (LOG_LEVEL > LOG_LEVEL_NO_LOG)
    // As Logger_message_safe() and Logger_printf_safe(), which take no va_list
    if (!Logger_filter(LOG_GLOBAL_PRX, level) ||
        !Logger_filter(module, level) ||
        !Logger_wait())
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    Logger_emit(LOG_GLOBAL_PRX, module, level, with_meta, with_meta, func, line, fmt, args);
    va_end(args);

    Logger_release();
#else
    (void)level;
    (void)with_meta;
    (void)module;
    (void)func;
    (void)line;
    (void)fmt;
#endif
}
