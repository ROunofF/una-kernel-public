// Host-test shim for the firmware Logger. The real Logger pulls in HAL/UART
// plumbing that is not available on a host build; for host unit tests every
// log macro expands to a no-op.

#ifndef HOST_TEST_LOGGER_LOGGER_H
#define HOST_TEST_LOGGER_LOGGER_H

#define LOG_LEVEL_NO_LOG   0
#define LOG_LEVEL_ERROR    1
#define LOG_LEVEL_WARNING  2
#define LOG_LEVEL_INFO     3
#define LOG_LEVEL_DEBUG    4

#define LOG_ERROR(...)       ((void)0)
#define LOG_WARNING(...)     ((void)0)
#define LOG_INFO(...)        ((void)0)
#define LOG_DEBUG(...)       ((void)0)
#define LOG_ERROR_WP(...)    ((void)0)
#define LOG_WARNING_WP(...)  ((void)0)
#define LOG_INFO_WP(...)     ((void)0)
#define LOG_DEBUG_WP(...)    ((void)0)

#endif // HOST_TEST_LOGGER_LOGGER_H
