/**
 * @file AirohaContextPool.cpp
 * @brief Command context pool implementation
 */

#include "AG3335/AirohaContextPool.hpp"

#define LOG_MODULE_PRX      "Airoha.ContextPool"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace Airoha
{

ContextPool::ContextPool()
{
    osMemoryPoolAttr_t attr {};
    attr.name = "ContextPool";
    attr.attr_bits = 0;
    attr.cb_mem = &mMemoryPoolCb;
    attr.cb_size = sizeof(StaticMemPool_t);
    attr.mp_mem = storage;
    attr.mp_size = sizeof(storage);

    mMemoryPoolId = osMemoryPoolNew(POOL_SIZE, sizeof(Storage), &attr);
    configASSERT(mMemoryPoolId != nullptr);

    LOG_DEBUG("Pool size %u bytes. Item %u bytes, Count %u\n",
            attr.mp_size, sizeof(Storage), POOL_SIZE);
}

ContextPool::~ContextPool()
{
    if (mMemoryPoolId != nullptr) {
        osMemoryPoolDelete(mMemoryPoolId);
    }
}

void ContextPool::returnToPool(Command::Context *ctx)
{
    if (ctx == nullptr) {
        return;
    }

    // Clean up semaphore if exists
    if (ctx->mCompletionToken != nullptr) {
        osSemaphoreDelete(ctx->mCompletionToken);
        ctx->mCompletionToken = nullptr;
    }

    // Call destructor explicitly
    ctx->~Context();

    // Return memory to pool
    osStatus_t status = osMemoryPoolFree(mMemoryPoolId, ctx);
    configASSERT(status == osOK);
}

size_t ContextPool::getFreeCount() const
{
    return osMemoryPoolGetSpace(mMemoryPoolId);
}

size_t ContextPool::getUsedCount() const
{
    return osMemoryPoolGetCount(mMemoryPoolId);
}

size_t ContextPool::getCapacity() const
{
    return osMemoryPoolGetCapacity(mMemoryPoolId);
}

void* ContextPool::allocate(uint32_t timeout)
{
    void* ptr = osMemoryPoolAlloc(mMemoryPoolId, timeout);
    if (ptr == nullptr) {
        LOG_ERROR("No memory!\n");
    }
    return ptr;
}

} // namespace Airoha
