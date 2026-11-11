/**
 * @file AirohaContextPool.hpp
 * @brief Command context pool implementation
 */

#pragma once

#include "AG3335/AirohaCommandContext.hpp"
#include "AG3335/AirohaCommandRegistry.hpp"

#include "cmsis_os.h"
#include "freertos_mpool.h"
#include <atomic>
#include <type_traits>

namespace Airoha
{

/**
 * @brief Pool of command contexts with placement new allocation
 */
class ContextPool {

public:
    ContextPool();

    ~ContextPool();

    // Non-copyable
    ContextPool(const ContextPool&) = delete;
    ContextPool& operator=(const ContextPool&) = delete;

    /**
     * @brief Acquire and construct command context
     * @tparam T Command type (must derive from CommandContext)
     * @param timeout Maximum time to wait for available context
     * @return Pointer to constructed command, or nullptr on timeout
     */
    template<typename T>
    T* acquire(uint32_t timeout = 0) {
        static_assert(std::is_base_of_v<Command::Context, T>,
                      "T must derive from CommandContext");
        static_assert(sizeof(T) <= MAX_COMMAND_SIZE,
                      "Command size exceeds MAX_COMMAND_SIZE - add command to ALL_GPS_COMMANDS in AirohaCommandRegistry.hpp");
        static_assert(alignof(T) <= MAX_COMMAND_ALIGN,
                      "Command alignment exceeds MAX_COMMAND_ALIGN");

        void* ptr = allocate(timeout);

        if (ptr != nullptr) {
            // Placement new to construct command in pre-allocated storage
            T* cmd = new (ptr) T();

            // Initialize base fields
            cmd->mRefCount = 1;
            cmd->mpOwnerPool = this;
            cmd->mCompletionToken = nullptr;
            cmd->mCompleted = false;
            cmd->mStatus = Command::Status::IntInvalid;
            cmd->mSequenceId = mSequenceCounter.fetch_add(1, std::memory_order_relaxed);

            return cmd;
        }

        return nullptr;
    }

    /**
     * @brief Return context to pool (internal use only)
     * @param ctx Context to return
     * @note Called automatically when ref_count reaches 0
     */
    void returnToPool(Command::Context* ctx);

    size_t getFreeCount() const;
    size_t getUsedCount() const;
    size_t getCapacity() const;

private:
    static constexpr size_t POOL_SIZE = 40;

    struct alignas(MAX_COMMAND_ALIGN) Storage {
        uint8_t data[MAX_COMMAND_SIZE];
    };

    osMemoryPoolId_t mMemoryPoolId {nullptr};
    StaticMemPool_t mMemoryPoolCb;
    Storage storage[POOL_SIZE];
    std::atomic<uint32_t> mSequenceCounter {0};

    void* allocate(uint32_t timeout);

};

} // namespace Airoha
