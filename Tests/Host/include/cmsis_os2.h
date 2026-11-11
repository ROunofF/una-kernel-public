/**
 * @file cmsis_os2.h
 * @brief Host stand-in for the slice of CMSIS-RTOS2 that MessagePool uses.
 *
 * Only the memory-pool API is provided, because that is all the code under test
 * touches. It is a real allocator, not a no-op: blocks come out of the caller's
 * storage at mp_mem + index * blockSize and go back on a free list, which is
 * what lets the host tests exercise MessagePool's address-to-index arithmetic
 * rather than merely compile it.
 *
 * Like the real FreeRTOS pool, freeing a block WRITES THE FREE-LIST LINK INTO
 * THE BLOCK'S FIRST WORD -- over the vptr of whatever used to live there. That
 * is deliberate: the ownership design turns on never handing out a block that
 * is already free, precisely because dispatching through that word would jump
 * through a free-list link. A stub that kept its links in a side array would
 * let those tests pass whether or not the protection existed.
 *
 * Known divergences from the target, none of which the tests rely on:
 *   - osMemoryPoolFree here VALIDATES alignment and range and returns an
 *     error; the real one accepts any in-range pointer and corrupts silently
 *     on a double free.
 *   - timeouts are ignored (MessagePool always passes 0) and no counting
 *     semaphore is modelled.
 *
 * Deliberately NOT defined here: osWaitForever, which Tests/Host/include/OS.hpp
 * already defines for the host build.
 */
#ifndef CMSIS_OS2_H
#define CMSIS_OS2_H

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos_mpool.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef configASSERT
#define configASSERT(x) assert(x)
#endif

typedef void* osMemoryPoolId_t;

typedef enum {
    osOK      =  0,
    osError   = -1,
    osErrorResource = -3,
    osErrorParameter = -4
} osStatus_t;

typedef struct {
    const char* name;
    uint32_t    attr_bits;
    void*       cb_mem;
    uint32_t    cb_size;
    void*       mp_mem;
    uint32_t    mp_size;
} osMemoryPoolAttr_t;

static inline osMemoryPoolId_t osMemoryPoolNew(uint32_t block_count,
                                               uint32_t block_size,
                                               const osMemoryPoolAttr_t* attr)
{
    if (attr == 0 || attr->cb_mem == 0 || attr->mp_mem == 0) {
        return 0;
    }
    if (block_count > HOST_MPOOL_MAX_BLOCKS || block_count == 0u) {
        return 0;
    }

    StaticMemPool_t* cb = (StaticMemPool_t*)attr->cb_mem;
    cb->mem        = (unsigned char*)attr->mp_mem;
    cb->blockSize  = block_size;
    cb->blockCount = block_count;
    cb->freeCount  = block_count;
    cb->freeHead   = 0u;

    /* Free list in ascending order, so the first allocations walk the storage
       from the bottom -- the same direction the real pool does. */
    for (uint32_t i = 0; i < block_count; ++i) {
        cb->next[i] = i + 1u;
    }
    cb->next[block_count - 1u] = UINT32_MAX;

    return (osMemoryPoolId_t)cb;
}

static inline void* osMemoryPoolAlloc(osMemoryPoolId_t mp_id, uint32_t timeout)
{
    (void)timeout;
    StaticMemPool_t* cb = (StaticMemPool_t*)mp_id;
    if (cb == 0 || cb->freeHead == UINT32_MAX) {
        return 0;
    }

    const uint32_t idx = cb->freeHead;
    cb->freeHead = cb->next[idx];
    cb->next[idx] = UINT32_MAX;
    --cb->freeCount;

    return (void*)(cb->mem + ((size_t)idx * cb->blockSize));
}

static inline osStatus_t osMemoryPoolFree(osMemoryPoolId_t mp_id, void* block)
{
    StaticMemPool_t* cb = (StaticMemPool_t*)mp_id;
    if (cb == 0 || block == 0) {
        return osErrorParameter;
    }

    const size_t off = (size_t)((unsigned char*)block - cb->mem);
    if ((off % cb->blockSize) != 0u) {
        return osErrorParameter;
    }

    const uint32_t idx = (uint32_t)(off / cb->blockSize);
    if (idx >= cb->blockCount) {
        return osErrorParameter;
    }

    cb->next[idx] = cb->freeHead;
    cb->freeHead  = idx;
    ++cb->freeCount;

    /* Write the link into the block itself, as FreeBlock() does on target, so
       a freed block's first word stops being a valid vptr. */
    {
        unsigned char* blk = cb->mem + ((size_t)idx * cb->blockSize);
        uintptr_t link = (cb->next[idx] == UINT32_MAX)
                       ? (uintptr_t)0
                       : (uintptr_t)(cb->mem + ((size_t)cb->next[idx] * cb->blockSize));
        size_t i;
        for (i = 0; i < sizeof(uintptr_t); ++i) {
            blk[i] = (unsigned char)((link >> (8u * i)) & 0xFFu);
        }
    }

    return osOK;
}

static inline uint32_t osMemoryPoolGetCapacity(osMemoryPoolId_t mp_id)
{
    StaticMemPool_t* cb = (StaticMemPool_t*)mp_id;
    return (cb == 0) ? 0u : cb->blockCount;
}

static inline uint32_t osMemoryPoolGetSpace(osMemoryPoolId_t mp_id)
{
    StaticMemPool_t* cb = (StaticMemPool_t*)mp_id;
    return (cb == 0) ? 0u : cb->freeCount;
}

static inline uint32_t osMemoryPoolGetCount(osMemoryPoolId_t mp_id)
{
    StaticMemPool_t* cb = (StaticMemPool_t*)mp_id;
    return (cb == 0) ? 0u : (cb->blockCount - cb->freeCount);
}

/* --- counting semaphore, enough for MessageManager's completion handshake --- */

typedef void* osSemaphoreId_t;

typedef struct {
    const char* name;
    uint32_t    attr_bits;
    void*       cb_mem;
    uint32_t    cb_size;
} osSemaphoreAttr_t;

typedef struct {
    uint32_t count;
    uint32_t maxCount;
} HostSemaphore_t;

static inline osSemaphoreId_t osSemaphoreNew(uint32_t max_count,
                                             uint32_t initial_count,
                                             const osSemaphoreAttr_t* attr)
{
    (void)attr;
    if (max_count == 0u) {
        return 0;
    }
    HostSemaphore_t* s = (HostSemaphore_t*)malloc(sizeof(HostSemaphore_t));
    if (s == 0) {
        return 0;
    }
    s->count    = initial_count;
    s->maxCount = max_count;
    return (osSemaphoreId_t)s;
}

static inline osStatus_t osSemaphoreAcquire(osSemaphoreId_t id, uint32_t timeout)
{
    (void)timeout;   /* nothing blocks on the host */
    HostSemaphore_t* s = (HostSemaphore_t*)id;
    if (s == 0) {
        return osErrorParameter;
    }
    if (s->count == 0u) {
        return osErrorResource;
    }
    --s->count;
    return osOK;
}

static inline osStatus_t osSemaphoreRelease(osSemaphoreId_t id)
{
    HostSemaphore_t* s = (HostSemaphore_t*)id;
    if (s == 0) {
        return osErrorParameter;
    }
    if (s->count >= s->maxCount) {
        return osErrorResource;
    }
    ++s->count;
    return osOK;
}

static inline osStatus_t osSemaphoreDelete(osSemaphoreId_t id)
{
    if (id == 0) {
        return osErrorParameter;
    }
    free(id);
    return osOK;
}

static inline osStatus_t osMemoryPoolDelete(osMemoryPoolId_t mp_id)
{
    StaticMemPool_t* cb = (StaticMemPool_t*)mp_id;
    if (cb == 0) {
        return osErrorParameter;
    }
    cb->blockCount = 0u;
    cb->freeCount  = 0u;
    cb->freeHead   = UINT32_MAX;
    return osOK;
}

#ifdef __cplusplus
}
#endif

#endif // CMSIS_OS2_H
