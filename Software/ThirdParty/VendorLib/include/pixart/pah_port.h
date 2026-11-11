/**
 ******************************************************************************
 * @file    pah_port.h
 * @brief   Functions the PixArt PPG library calls that the firmware must
 *          provide.
 ******************************************************************************
 */

#ifndef PAH_PORT_H
#define PAH_PORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Write one driver log message.
 * @param   level       1 error, 2 warning, 3 info, 4 debug
 * @param   with_meta   true for a complete message, which the firmware may
 *                      prefix with a timestamp, level, module, function and
 *                      line; false for a continuation of the previous one,
 *                      to be written as it is
 * @param   module      Name of the driver module that logged it
 * @param   func        Function that logged it, NULL when with_meta is false
 * @param   line        Source line, 0 when with_meta is false
 * @param   fmt         printf-style format; the message carries its own '\n'
 */
void pah_port_log(uint8_t level, bool with_meta, const char *module,
                  const char *func, int line, const char *fmt, ...)
        __attribute__((format(printf, 6, 7)));

#ifdef __cplusplus
}
#endif

#endif /* PAH_PORT_H */
