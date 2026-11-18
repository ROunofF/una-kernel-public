// Host-test shim for the firmware Logger_fwd.h, as Logger.h next to it is for
// Logger.h: the LOGM_* family expands to no-ops.

#ifndef HOST_TEST_LOGGER_LOGGER_FWD_H
#define HOST_TEST_LOGGER_LOGGER_FWD_H

#define LOG_LEVEL_NO_LOG   0
#define LOG_LEVEL_ERROR    1
#define LOG_LEVEL_WARNING  2
#define LOG_LEVEL_INFO     3
#define LOG_LEVEL_DEBUG    4

#define LOGM(...)          ((void)0)
#define LOGM_WP(...)       ((void)0)

#endif // HOST_TEST_LOGGER_LOGGER_FWD_H
