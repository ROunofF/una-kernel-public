/**
 * @file AirohaCommandContext.hpp
 * @brief GPS command context with intrusive reference counting
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>
#include "cmsis_os.h"

// Forward declarations
namespace Airoha {
class ContextPool;
class AirohaBase;
}

namespace Airoha::Command
{

/**
 * @brief Command execution status codes
 *
 * Combines internal driver errors (negative values) and Airoha GPS module
 * response codes (non-negative values).
 *
 * @note Negative codes: driver errors; Non-negative: GPS module responses
 */
enum class Status : int8_t {
    // Internal errors
    IntError            = -5,  ///< Internal processing error
    IntParsingError     = -4,  ///< Response parsing failed
    IntInvalidParameter = -3,  ///< Invalid command parameter
    IntTimeout          = -2,  ///< GPS response timeout
    IntInvalid          = -1,  ///< Invalid context state

    // Airoha GPS module codes
    Success             = 0,   ///< Command successful
    Processing          = 1,   ///< Command processing
    Failed              = 2,   ///< Command failed
    CmdIdNotSupported   = 3,   ///< Unsupported command
    ParameterError      = 4,   ///< Parameter error
    ServiceBusy         = 5,   ///< Service busy
};


/**
 * @brief Base command context with intrusive reference counting
 *
 * @details
 * CommandContext provides a thread-safe reference-counted wrapper for GPS commands.
 * It supports both asynchronous (fire-and-forget) and synchronous (blocking) execution modes.
 *
 * Reference Counting Lifecycle:
 * - Contexts are acquired from a pre-allocated pool using placement new
 * - Initial mRefCount = 1 (owned by initiator)
 * - When queued, mRefCount = 2 (owned by initiator + worker queue)
 * - Both initiator and worker call release() when done
 * - Last release() automatically returns context to pool
 *
 * @par Asynchronous Command Lifecycle:
 * @code
 * 1. ContextPool::acquire<CommandType>()
 *    - Allocates storage from pool
 *    - Constructs command via placement new
 *    - mRefCount = 1 (initiator owns)
 *    - mCompletionToken = nullptr
 *
 * 2. GpsDriver::sendCommandAsync(ctx)
 *    - ctx->addRef() -> mRefCount = 2
 *    - Queue ctx pointer
 *    - ctx->release() -> mRefCount = 1 (initiator done)
 *
 * 3. Worker dequeues ctx
 *    - mRefCount = 1 (worker owns)
 *    - formatCommandPayload()
 *    - Send to GPS module
 *    - Wait for response
 *    - parseResponsePayload()
 *
 * 4. Worker calls ctx->release()
 *    - mRefCount = 0
 *    - Calls ContextPool::returnToPool()
 *    - Destructor called
 *    - Memory returned to pool
 * @endcode
 *
 * @par Synchronous Command Lifecycle (Success):
 * @code
 * 1. ContextPool::acquire<CommandType>()
 *    - mRefCount = 1 (initiator owns)
 *
 * 2. GpsDriver::sendCommandSync(ctx, timeout)
 *    - Creates binary semaphore: ctx->mCompletionToken = osSemaphoreNew(1, 0, ...)
 *    - ctx->addRef() -> mRefCount = 2
 *    - Queue ctx pointer
 *    - osSemaphoreAcquire(mCompletionToken, timeout) [BLOCKS]
 *
 * 3. Worker dequeues ctx
 *    - mRefCount = 2 (initiator + worker)
 *    - Process command
 *    - parseResponsePayload()
 *    - osSemaphoreRelease(mCompletionToken) [SIGNAL]
 *    - ctx->release() -> mRefCount = 1
 *
 * 4. Initiator wakes up
 *    - osSemaphoreAcquire() returns osOK
 *    - Read response data from ctx
 *    - ctx->release() -> mRefCount = 0
 *    - ContextPool::returnToPool()
 *    - osSemaphoreDelete(mCompletionToken)
 *    - Destructor called
 *    - Memory returned to pool
 * @endcode
 *
 * @par Synchronous Command Lifecycle (Timeout):
 * @code
 * 1-2. Same as success case
 *
 * 3. Initiator timeout
 *    - osSemaphoreAcquire() returns osErrorTimeout
 *    - ctx->release() -> mRefCount = 1 (worker still owns)
 *    - Initiator returns error
 *
 * 4. Worker eventually completes
 *    - osSemaphoreRelease(mCompletionToken) [nobody waiting]
 *    - ctx->release() -> mRefCount = 0
 *    - ContextPool::returnToPool()
 *    - osSemaphoreDelete(mCompletionToken)
 *    - Memory returned to pool
 * @endcode
 *
 * @par Thread Safety:
 * - Reference counting uses atomic operations (acquire/release semantics)
 * - Multiple threads can safely call addRef() and release()
 * - Only the last release() (mRefCount 1->0) returns to pool
 * - Pool allocation/deallocation is thread-safe via osMemoryPool
 *
 * @warning
 * - Never manually delete CommandContext - always use release()
 * - Never access context after calling release() (unless holding another reference)
 * - Derived classes must call setCmdId() in constructor
 * - Only call setStatus() during parseResponsePayload() or when receiving an error code from the GPS
 *
 * @note
 * Contexts use placement new over pre-allocated storage to avoid heap fragmentation.
 * Maximum command size is determined at compile-time via ALL_GPS_COMMANDS registry.
 *
 * @see ContextPool
 * @see GpsDriver::sendCommandAsync()
 * @see GpsDriver::sendCommandSync()
 */
class Context {

public:
    virtual ~Context() = default;

    // Non-copyable, non-movable
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    /**
     * @brief Add reference to this context
     * @note Thread-safe. Uses atomic increment with relaxed ordering.
     */
    void addRef();

    /**
     * @brief Release reference to this context
     * @note Thread-safe. Returns context to pool when mRefCount reaches 0.
     * @warning Do not access context after calling release() unless holding another reference.
     */
    void release();

    /**
     * @brief Format command payload for transmission to GPS module
     * @param buffer Output buffer for formatted command
     * @param size Size of output buffer
     * @return Actual payload length in bytes
     * @note Called by worker thread before sending command to GPS
     */
    virtual size_t formatCommandPayload(char* buffer, size_t size) {return 0;}

    /**
     * @brief Parse response payload from GPS module
     * @param tokens Response split into tokens
     * @param count Number of tokens
     * @retval true Response parsed successfully, setStatus() called
     * @retval false Parse error occurred
     * @note Called on the RX thread while AirohaBase::mCmdHandlerMutex is held
     *       (via the active command handler). Overrides MUST do parsing only and
     *       MUST NOT send commands (sendRaw/sendGeneric, directly or indirectly):
     *       the worker thread re-acquires the same mutex, so a send from here
     *       would deadlock.
     */
    virtual bool parseResponsePayload(const std::string_view* tokens, size_t count) {return true;}

    /**
     * @brief Check if command completed successfully
     * @retval true Command status is Success
     * @retval false Command failed or still processing
     */
    bool isSuccess() const { return mStatus == Status::Success; }

    /**
     * @brief Get current command status
     * @return Command status code
     */
    Status getStatus() const { return mStatus; }

    /**
     * @brief Get command identifier
     * @return Command ID string (e.g., "062" for SetPositionRate)
     */
    std::string_view getCmdId() const { return mCmdId; }

    /**
     * @brief Set command execution timeout (including ACK and RESPONSE)
     * @param ms New execution timeout in milliseconds.
     */
    void setTimeout(uint32_t ms) { mTimeoutMs = ms; }

    /**
     * @brief Get command execution timeout
     * @return execution timeout in milliseconds.
     */
    uint32_t getTimeout() { return mTimeoutMs; }

    /**
     * @brief Mark that command should wait for a response.
     */
    void setHasResponse() { mHasResponse = true; }

    /**
     * @brief Check if command has RESPONSE in additional to ACK
     * @retval true if command should wait for a response.
     * @retval true if command should wait ACK only.
     */
     bool hasResponce() { return mHasResponse; }

    /**
     * @brief Get unique sequence identifier
     * @return Sequence ID assigned by pool during acquisition
     */
    uint32_t getSequenceId() const { return mSequenceId; }

    /**
     * @brief Check if command processing completed
     * @retval true Worker finished processing (success or error)
     * @retval false Still processing or queued
     * @note Thread-safe
     */
    bool isCompleted() const { return mCompleted.load(std::memory_order_acquire); }

protected:
    /**
     * @brief Protected constructor - only derived classes can construct
     */
    Context() = default;

    /**
     * @brief Set command ID (should be called once in derived constructor)
     * @param cmdId Command identifier string
     */
    void setCmdId(std::string_view cmdId) { mCmdId = cmdId; }

    /**
     * @brief Set command status (called during response parsing)
     * @param status New status code
     */
    void setStatus(Status status) { mStatus = status; }

private:
    // Reference counting
    std::atomic<uint32_t> mRefCount {0};         ///< Atomic reference counter
    ::Airoha::ContextPool *mpOwnerPool {nullptr};          ///< Backlink to owning pool

    // Synchronization
    osSemaphoreId_t mCompletionToken {nullptr};  ///< Binary semaphore for sync mode
    std::atomic<bool> mCompleted {false};        ///< Completion flag

    // Command metadata
    std::string_view mCmdId {""};                ///< Command identifier
    uint32_t mSequenceId {0};                    ///< Unique sequence number
    uint32_t mTimeoutMs {500};                   ///< Command execution timeout
    bool mHasResponse {false};                   ///< Command has RESPONSE in additional to ACK

    // Status
    Status mStatus {Status::IntInvalid};         ///< Current command status

    friend class ::Airoha::ContextPool;
    friend class ::Airoha::AirohaBase;
};

} // namespace Airoha::Command
