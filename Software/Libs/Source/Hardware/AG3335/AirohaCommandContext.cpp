/**
 * @file AirohaCommandContext.cpp
 * @brief GPS command context with intrusive reference counting
 */

#include "AG3335/AirohaCommandContext.hpp"
#include "AG3335/AirohaContextPool.hpp"

namespace Airoha::Command
{

void Context::addRef()
{
    mRefCount.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief Release reference to this context
 * @note Thread-safe. Returns context to pool when mRefCount reaches 0.
 * @warning Do not access context after calling release() unless holding another reference.
 */
void Context::release()
{
    uint32_t oldCount = mRefCount.fetch_sub(1, std::memory_order_acq_rel);

    if (oldCount == 1) {
        // Last reference, return to pool
        configASSERT(mpOwnerPool != nullptr);
        if (mpOwnerPool != nullptr) {
            mpOwnerPool->returnToPool(this);
        }
    }
}

} // namespace Airoha::Command
