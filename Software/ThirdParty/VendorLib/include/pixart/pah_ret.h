/**
 ******************************************************************************
 * @file    pah_ret.h
 * @brief   Result codes returned by the PAH831x driver and its platform hooks.
 ******************************************************************************
 */

#ifndef PAH_RET_H
#define PAH_RET_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PAH_RET_SUCCESS = 0,
    PAH_RET_PENDING,
    PAH_RET_FAILED,             ///< Unspecified failure inside the driver
    PAH_RET_PLAT_FAILED,        ///< A platform hook (I2C read/write) failed
    PAH_RET_VERIFY_FAILED,      ///< The chip did not identify as a PAH831x
    PAH_RET_NOT_IMPLEMENTED,
    PAH_RET_INVALID_ARGUMENT,
    PAH_RET_INVALID_OPERATION,  ///< Not allowed in the driver's current state
    PAH_RET_FIFO_CKS_FAILED,    ///< FIFO data failed its checksum
    PAH_RET_FIFO_OVERFLOW,      ///< The chip's FIFO overflowed
    PAH_RET_NULL_PTR,
    PAH_RET_ASYNC_PENDING,
} pah_ret;

#define PAH_SUCCEEDED(ret)  ((ret) == PAH_RET_SUCCESS)
#define PAH_FAILED(ret)     (!PAH_SUCCEEDED(ret))

#define PAH_CHECK_RET(ret) \
    if (PAH_FAILED(ret)) { \
        return ret; \
    }

#define PAH_CHECK_NULL(p) \
    if (!(p)) { \
        return PAH_RET_INVALID_ARGUMENT; \
    }

#ifdef __cplusplus
}
#endif

#endif /* PAH_RET_H */
