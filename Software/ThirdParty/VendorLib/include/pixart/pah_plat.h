/**
 ******************************************************************************
 * @file    pah_plat.h
 * @brief   Platform hooks the PAH831x driver calls: register access over the
 *          bus, delays and a millisecond tick.
 ******************************************************************************
 */

#ifndef PAH_PLAT_H
#define PAH_PLAT_H

#include "pah_plat_inc.h"
#include "pah_ret.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PAH_PLAT_COMM_I2C,
    PAH_PLAT_COMM_SPI,
    PAH_PLAT_COMM_I3C,
} pah_plat_comm_type;

/**
 * Passed to pah831x_sensor_init(). The driver keeps a copy, and passes
 * user_data back as pArgs to every hook.
 */
typedef struct {
    void               *user_data;
    pah_plat_comm_type  type;
    size_t              max_length;     ///< Longest single bus transfer, in bytes
    pah_ret           (*write)(uint8_t addr, const uint8_t *data, size_t write_size, void *pArgs);
    pah_ret           (*read)(uint8_t addr, uint8_t *data, size_t read_size, void *pArgs);
    void              (*delay_ms)(uint32_t ms, void *pArgs);
    uint32_t          (*ticks)(void *pArgs);     ///< Milliseconds since an arbitrary epoch
} pah_plat;

#ifdef __cplusplus
}
#endif

#endif /* PAH_PLAT_H */
