
#include "App/Logger.hpp"

#include "Logger.h"

#include <cstdio>

namespace App
{

Logger::Logger(const char *name) : mName(name)
{
}

void Logger::printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    this->vprintf(format, args);
    va_end(args);
}

void Logger::vprintf(const char *format, va_list args)
{
    if (Logger_filter(mName, LOG_LEVEL_NO_LOG) && Logger_wait()) {
        Logger_emit(mName, "", 0, true, false, NULL, 0, format, args);
        Logger_release();
    }
}

void Logger::mvprintf(const char *level,
        const char *module_name,
        const char *func, int line,
        const char *fmt, va_list args)
{
    // Temporary solution.
    // TODO: change 'const char *level' to 'uint8_t level' in the interface
    uint8_t lvl = LOG_LEVEL_DEBUG;
    if (strcmp(level, "E")==0) {lvl = LOG_LEVEL_ERROR;}
    else if (strcmp(level, "W")==0) {lvl = LOG_LEVEL_WARNING;}
    else if (strcmp(level, "I")==0) {lvl = LOG_LEVEL_INFO;}

    if (Logger_filter(mName, lvl) && Logger_wait()) {
        Logger_emit(mName, module_name, lvl, true, true, func, line, fmt, args);
        Logger_release();
    }
}

} // namespace App
