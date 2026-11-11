/**
 * @file freertos_mpool.h
 * @brief Host stand-in for the CMSIS-RTOS2 FreeRTOS memory-pool control block.
 *
 * On target this type is opaque to us -- it is FreeRTOS's own control block and
 * MessagePool only ever provides storage for it. Here it IS the pool: the host
 * cmsis_os2.h stub uses the caller's cb_mem as its bookkeeping, which keeps the
 * stub allocation-free and matches the static-memory contract of the real one.
 *
 * The layout that matters for the code under test is the BLOCK layout, not this
 * one: blocks are handed out at mp_mem + index * blockSize with no per-block
 * header, exactly as CreateBlock() does in the real cmsis_os2.c. MessagePool's
 * address-to-index arithmetic depends on that, so the stub reproduces it
 * deliberately rather than incidentally.
 */
#ifndef FREERTOS_MPOOL_H
#define FREERTOS_MPOOL_H

#include <stdint.h>

/// Largest blockCount in POOL_CONFIGS, with slack. Checked at pool creation,
/// which surfaces as the configASSERT in MessagePool::initPool.
#define HOST_MPOOL_MAX_BLOCKS 256u

typedef struct {
    unsigned char* mem;         ///< caller-provided block storage
    uint32_t       blockSize;
    uint32_t       blockCount;
    uint32_t       freeHead;    ///< index of the first free block, or UINT32_MAX
    uint32_t       freeCount;
    uint32_t       next[HOST_MPOOL_MAX_BLOCKS];  ///< index-based free list
} StaticMemPool_t;

#endif // FREERTOS_MPOOL_H
