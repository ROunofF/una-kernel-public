/**
 * @file MessageManager.hpp
 * @brief Message lifecycle management and pool allocation
 *
 * MessageManager handles:
 * - Memory allocation from pools
 * - Reference counting
 * - Completion synchronization (semaphores)
 * - Message lifecycle (allocation to deallocation)
 *
 */

#ifndef __APP_MESSAGE_MANAGER_HPP
#define __APP_MESSAGE_MANAGER_HPP

#include <cstdint>
#include <atomic>

#include "SDK/Messages/MessageBase.hpp"

#include "App/LogThrottle.hpp"
#include "App/MessagePool.hpp"

namespace App
{

/**
 * @brief Message lifecycle manager
 *
 * Manages message allocation, reference counting, and cleanup.
 * Uses MessagePool for memory allocation.
 * Handles completion synchronization for request-response patterns.
 */
class MessageManager {
public:
    /**
     * @brief Construct message manager
     */
    MessageManager();

    /**
     * @brief Destructor
     */
    ~MessageManager() = default;

    // =========================================================================
    // Message allocation and deallocation
    // =========================================================================

    /**
     * @brief Allocate raw memory from pool
     * @param size Required message size in bytes
     * @retval Pointer to allocated raw memory on success
     * @retval nullptr if allocation failed
     * @note Memory is zeroed
     * @note Caller must use placement new to construct message
     */
    void* allocateRawMemory(size_t size);

    /**
     * @brief Allocate typed message object from pool
     * @retval Pointer to typed message on success
     * @retval nullptr if allocation failed
     *
     * @note Template wrapper for type-safe allocation
     * @note Calls placement new on allocated memory
     * @note Pool determined automatically by address during deallocation
     * @note Example: auto* msg = msgMgr.allocateMessage<GpsRequestMsg>();
     */
    template<typename T>
    T* allocateMessage() {
        void* ptr = allocateRawMemory(sizeof(T));
        if (ptr) {
            // Placement new to construct typed object
            return new (ptr) T();
        }
        return nullptr;
    }

    /**
     * @brief Release message back to pool
     * @param msg Message to release
     * @note Decrements reference count
     * @note Returns to pool when refCount reaches zero
     * @note Cleans up completion semaphore if exists
     * @note Thread-safe operation
     */
    void releaseMessage(SDK::MessageBase* msg);

    /**
     * @brief   Drop a dead process's references to any messages it still held.
     * @param   pid: The process that has gone away.
     * @retval  Number of claims acted on -- references dropped, plus blocks
     *          reclaimed that were never constructed, plus stale claims that
     *          were refused. Each of the three is logged separately, because
     *          only the first is routine.
     *
     * A message queued for a process becomes that process's to return. If it
     * dies first -- killed by Loader::unload, or simply exiting with messages
     * parked in the SDK's own user queue -- nothing else knows those blocks
     * exist and the pool loses them for the life of the boot.
     *
     * This is the backstop. It drops exactly the dead process's references, so
     * a message somebody else still holds survives correctly and one that was
     * only the app's is destroyed and returned to the pool through the normal
     * path.
     *
     * PRECONDITION: @p pid's thread has been terminated. Only that process
     * adds or removes its own claims, so once it is gone they are stable and
     * the sweep races nobody. Call this after Loader::unload(), never before.
     *
     * It does NOT need the kernel-side queues drained first, and cannot
     * conflict with a drain. Not because they touch different BLOCKS -- a
     * message an app allocated and then sent still carries that app's claim
     * while it sits in a queue -- but because they give back different
     * REFERENCES. A drain returns the queue's reference; a sweep returns the
     * dead process's own. Neither can return the other's, so the two may run
     * in either order, or not at all.
     */
    size_t releaseByOwner(uint32_t pid);

    /**
     * @brief   Note that @p pid is now responsible for returning @p msg.
     *
     * Called by a process on its own thread where it takes on a reference:
     * allocation, and the pop in IAppComm::getMessage.
     */
    void addMessageOwner(SDK::MessageBase* msg, uint32_t pid);

    /**
     * @brief   Note that @p pid has returned the reference it owed for @p msg.
     *
     * The other half of addMessageOwner. A claim must not outlive the
     * reference it stands for: a block still claimed after its owner has
     * released is one a sweep would free while somebody else is using it.
     */
    void removeMessageOwner(SDK::MessageBase* msg, uint32_t pid);

    /**
     * @brief Retain message (increment reference count)
     * @param msg Message to retain
     * @note Thread-safe operation
     */
    void retainMessage(SDK::MessageBase* msg);

    // =========================================================================
    // Message completion synchronization
    // =========================================================================

    /**
     * @brief Initialize completion semaphore for message
     * @param msg Message to initialize semaphore for
     * @retval true if semaphore created successfully
     * @retval false if creation failed
     * @note Creates dynamic semaphore (FreeRTOS allocates memory)
     */
    bool initCompletionSemaphore(SDK::MessageBase* msg);

    /**
     * @brief Wait for message completion
     * @param msg Message to wait for
     * @param timeoutMs Timeout in milliseconds
     * @retval true if completed before timeout
     * @retval false if timeout occurred
     */
    bool waitCompletion(SDK::MessageBase* msg, uint32_t timeoutMs);

    /**
     * @brief Signal message completion
     * @param msg Message to signal
     * @note Sets completed flag and releases semaphore
     */
    void signalCompletion(SDK::MessageBase* msg);

    /**
     * @brief Signal message completion with processing result
     * @param msg Message to signal
     * @param result Result status
     * @note Equivalent to msg->setResult() + signalCompletion()
     */
    void signalCompletion(SDK::MessageBase* msg, SDK::MessageResult result);

    /**
     * @brief Cleanup completion semaphore
     * @param msg Message to cleanup semaphore for
     * @note Deletes semaphore if it exists
     */
    void cleanupCompletionSemaphore(SDK::MessageBase* msg);

    // =========================================================================
    // Pool statistics and management
    // =========================================================================

    /**
     * @brief Get message pool reference
     * @retval Reference to underlying message pool
     * @note For accessing pool statistics
     */
    App::MessagePool& getPool() { return mMessagePool; }

    /**
     * @brief Get message pool reference (const)
     * @retval Reference to underlying message pool
     */
    const App::MessagePool& getPool() const { return mMessagePool; }

private:
    App::MessagePool mMessagePool;  // Shared message pool

    /// See removeMessageOwner(). Latched so the error is reported once per
    /// boot. Atomic for the same reason the pool's exhaustion counter is:
    /// every process's release path writes it, and two flags serving the same
    /// once-per-boot purpose should not disagree about whether that matters.
    std::atomic<bool> mOrphanReleaseLogged{false};

    /// allocateRawMemory() failures, throttled, one per pool plus one
    /// for a size no pool fits. Per pool because allocate() never falls back
    /// to a larger one: a single pool can run dry while the others keep
    /// succeeding, and those are separate conditions. Not per app, because the
    /// pools are shared - one app exhausting a pool makes every caller of that
    /// size fail at once, and one line per window is what that deserves.
    LogThrottle mAllocFailLog[POOL_COUNT + 1];

    // Prevent copying
    MessageManager(const MessageManager&) = delete;
    MessageManager& operator=(const MessageManager&) = delete;
};

} // namespace App

#endif // __APP_MESSAGE_MANAGER_HPP
