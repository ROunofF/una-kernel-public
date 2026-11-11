/******************************************************************************
 * @file WorkerPool.hpp
 * @brief Worker pool with CMSIS-RTOS v2 ISR-safe operations and flexible
 *        stack size management
 *****************************************************************************/

#pragma once

#include "cmsis_os2.h"
#include <cstdint>
#include <cstdio>

namespace OS {

/**
 * @brief Worker priority levels
 */
enum class WorkerPriority : uint8_t {
    NORMAL = 0,
    HIGH = 1,
    NUM_PRIORITIES = 2
};

/**
 * @brief Worker handle type (opaque pointer)
 */
typedef void* WorkerHandle_t;

/**
 * @brief Worker callback function type
 * @param handle Worker handle that triggered the callback
 */
typedef void (*WorkerCallback_t)(WorkerHandle_t handle);

/**
 * @brief Worker execution mode
 */
enum class WorkerMode : uint8_t {
    ONE_SHOT,   /* Execute once when triggered */
    PERIODIC    /* Execute periodically with given period */
};

/**
 * @brief Forced execution behavior for periodic workers
 */
enum class ForceExecuteMode : uint8_t {
    RESET_TIMER,     /* Reset timer to full period after forced execution */
    CONTINUE_TIMER   /* Keep original timer schedule */
};

/**
 * @brief Worker thread configuration
 */
struct WorkerThreadConfig {
    uint16_t stack_bytes;       /* Stack size in bytes */
    WorkerPriority priority;    /* Thread priority */
};

/**
 * @brief Worker pool configuration
 */
struct WorkerPoolConfig {
    uint16_t command_queue_length;              /* Command queue size */
    WorkerThreadConfig threads[6];              /* Up to 6 worker threads */
    uint8_t num_threads;                        /* Actual number of threads */
};

/**
 * @brief Base class for worker callbacks with context
 *
 * Inherit from this class to use class methods as worker callbacks:
 *
 * @code
 * class MyClass : public OS::WorkerCallbackBase {
 * public:
 *     void onWorkerExecute(OS::WorkerHandle_t handle) override {
 *         // Your implementation here
 *     }
 * };
 * @endcode
 */
class WorkerCallbackBase {
public:
    virtual ~WorkerCallbackBase() = default;

    /**
     * @brief Worker execution callback
     * @param handle Worker handle that triggered the callback
     */
    virtual void onWorkerExecute(WorkerHandle_t handle) = 0;
};

/**
 * @brief Worker pool class - manages multiple worker threads with flexible
 *        stack sizes and ISR-safe operations
 *
 * Features:
 * - Multiple worker threads with configurable stack sizes
 * - Automatic thread selection based on stack requirements
 * - ISR-safe operations via CMSIS-RTOS v2
 * - Support for both function and class method callbacks
 * - Periodic and one-shot execution modes
 * - Priority-based execution
 * - Protection against multiple initialization
 *
 * Stack Size Selection:
 * - Min stack = 0: Automatically selects smallest available thread
 * - Min stack > 0: Selects smallest thread with sufficient stack
 * - Min stack > max available: Worker creation fails
 *
 * Usage:
 * @code
 * // Initialize pool (only once)
 * OS::WorkerPool::GetInstance().init(OS::DEFAULT_WORKER_POOL_CONFIG);
 *
 * // Create worker - let pool choose smallest thread
 * OS::WorkerHandle_t simple = OS::WorkerPool::GetInstance().createWorker(
 *     "simple", 1000, OS::WorkerMode::PERIODIC,
 *     OS::WorkerPriority::NORMAL, 0, simpleCallback  // 0 = auto-select
 * );
 *
 * // Use from ISR (completely safe!)
 * void SomeIRQHandler() {
 *     OS::WorkerPool::GetInstance().forceExecute(simple);
 * }
 * @endcode
 */
class WorkerPool {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to worker pool
     */
    static WorkerPool& GetInstance();

    /**
     * @brief Initialize worker pool with configuration
     *
     * Can only be called once. Subsequent calls will return false.
     *
     * @param config Pool configuration defining available worker threads
     * @retval true if initialization successful
     * @retval false if already initialized or initialization failed
     */
    bool init(const WorkerPoolConfig& config);

    /**
     * @brief Check if worker pool is initialized
     * @retval true if pool has been successfully initialized
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Create worker with function callback
     * @param name Worker name for debugging (max 15 characters)
     * @param period_ms Period in milliseconds (for periodic workers)
     * @param mode ONE_SHOT or PERIODIC
     * @param priority Worker priority level (NORMAL or HIGH)
     * @param min_stack_bytes Minimum stack size required in bytes
     *                        - 0: Auto-select smallest available thread
     *                        - >0: Select smallest thread with >= min_stack_bytes
     * @param callback Function to call when worker executes
     * @return Worker handle or nullptr on failure
     *
     * Returns nullptr if:
     * - Pool not initialized
     * - No thread available with sufficient stack
     * - min_stack_bytes exceeds maximum available stack
     * - No thread available with matching priority
     *
     * Example - auto-select stack:
     * @code
     * // Simple flag setter - don't care about stack size
     * OS::WorkerHandle_t flag_worker = pool.createWorker(
     *     "flag", 1000, OS::WorkerMode::PERIODIC,
     *     OS::WorkerPriority::NORMAL, 0, flagCallback  // 0 = auto
     * );
     * @endcode
     *
     * Example - specific stack requirement:
     * @code
     * // GPS parser needs 3KB for buffers
     * OS::WorkerHandle_t gps_worker = pool.createWorker(
     *     "gps", 0, OS::WorkerMode::ONE_SHOT,
     *     OS::WorkerPriority::NORMAL, 3072, gpsCallback  // Need 3KB
     * );
     * @endcode
     */
    WorkerHandle_t createWorker(const char* name,
                                uint32_t period_ms,
                                WorkerMode mode,
                                WorkerPriority priority,
                                uint16_t min_stack_bytes,
                                WorkerCallback_t callback);

    /**
     * @brief Create worker with class method callback
     * @param name Worker name for debugging (max 15 characters)
     * @param period_ms Period in milliseconds (for periodic workers)
     * @param mode ONE_SHOT or PERIODIC
     * @param priority Worker priority level (NORMAL or HIGH)
     * @param min_stack_bytes Minimum stack size required in bytes (0 = auto)
     * @param callback_obj Pointer to object implementing WorkerCallbackBase
     * @return Worker handle or nullptr on failure
     *
     * Example:
     * @code
     * class Sensor : public OS::WorkerCallbackBase {
     *     void onWorkerExecute(OS::WorkerHandle_t handle) override { ... }
     * };
     *
     * Sensor sensor;
     * OS::WorkerHandle_t worker = pool.createWorker(
     *     "sensor", 1000, OS::WorkerMode::PERIODIC,
     *     OS::WorkerPriority::NORMAL, 0, &sensor  // Auto-select
     * );
     * @endcode
     */
    WorkerHandle_t createWorker(const char* name,
                                uint32_t period_ms,
                                WorkerMode mode,
                                WorkerPriority priority,
                                uint16_t min_stack_bytes,
                                WorkerCallbackBase* callback_obj);

    /* ===================================================================== */
    /* Control Functions - Safe to call from BOTH task and ISR context      */
    /* CMSIS-RTOS v2 automatically detects context and uses correct API     */
    /* ===================================================================== */

    /**
     * @brief Start worker
     *
     * Safe to call from both task and ISR context.
     * CMSIS-RTOS v2 automatically handles context detection.
     *
     * @param handle Worker handle
     * @param delay_ms Delay before first execution (0 = immediate)
     * @retval true if command posted successfully
     *
     * Example from task:
     * @code
     * void someTask() {
     *     OS::WorkerPool::GetInstance().startWorker(gps_worker, 100);
     * }
     * @endcode
     *
     * Example from ISR:
     * @code
     * void UART_IRQHandler() {
     *     if (data_ready) {
     *         OS::WorkerPool::GetInstance().startWorker(gps_worker, 0);
     *     }
     * }
     * @endcode
     */
    bool startWorker(WorkerHandle_t handle, uint32_t delay_ms = 0);

    /**
     * @brief Stop worker
     *
     * Safe to call from both task and ISR context.
     *
     * @param handle Worker handle
     * @retval true if command posted successfully
     */
    bool stopWorker(WorkerHandle_t handle);

    /**
     * @brief Reset worker timer
     *
     * Resets the timer to the full period, starting from now.
     * Safe to call from both task and ISR context.
     *
     * @param handle Worker handle
     * @retval true if command posted successfully
     *
     * Example from ISR:
     * @code
     * void TouchIRQHandler() {
     *     // Reset display timeout on touch
     *     OS::WorkerPool::GetInstance().resetWorker(display_timeout);
     * }
     * @endcode
     */
    bool resetWorker(WorkerHandle_t handle);

    /**
     * @brief Change worker period
     *
     * Changes the period for periodic workers. The new period takes effect
     * from the next execution.
     * Safe to call from both task and ISR context.
     *
     * @param handle Worker handle
     * @param new_period_ms New period in milliseconds
     * @retval true if command posted successfully
     *
     * Example:
     * @code
     * // Adjust heart rate sampling based on activity
     * if (high_activity) {
     *     OS::WorkerPool::GetInstance().changePeriod(hr_worker, 500);  // 0.5s
     * } else {
     *     OS::WorkerPool::GetInstance().changePeriod(hr_worker, 2000); // 2s
     * }
     * @endcode
     */
    bool changePeriod(WorkerHandle_t handle, uint32_t new_period_ms);

    /**
     * @brief Delete worker
     *
     * Deletes the worker and frees associated resources.
     * Safe to call from both task and ISR context.
     *
     * @param handle Worker handle
     * @retval true if command posted successfully
     */
    bool deleteWorker(WorkerHandle_t handle);

    /**
     * @brief Force immediate execution of worker
     *
     * Executes the worker callback immediately in the worker thread context,
     * not in the caller's context. This is thread-safe and can be called
     * from any context including ISRs.
     *
     * Behavior by worker mode:
     * - ONE_SHOT: Callback executes, worker becomes inactive
     * - PERIODIC + RESET_TIMER: Callback executes, timer resets to full period
     * - PERIODIC + CONTINUE_TIMER: Callback executes, original schedule continues
     *
     * Safe to call from both task and ISR context.
     *
     * @param handle Worker handle
     * @param mode Timer behavior for PERIODIC workers (ignored for ONE_SHOT)
     * @retval true if command posted successfully
     *
     * Example from ISR:
     * @code
     * void Button_IRQHandler() {
     *     // User pressed "measure now" button
     *     OS::WorkerPool::GetInstance().forceExecute(
     *         heart_rate_worker,
     *         OS::ForceExecuteMode::RESET_TIMER
     *     );
     * }
     * @endcode
     */
    bool forceExecute(WorkerHandle_t handle,
                     ForceExecuteMode mode = ForceExecuteMode::RESET_TIMER);

    /* ===================================================================== */
    /* Helper Functions                                                      */
    /* ===================================================================== */

    /**
     * @brief Set worker context data
     * @param handle Worker handle
     * @param context User context pointer
     */
    void setContext(WorkerHandle_t handle, void* context);

    /**
     * @brief Get worker context data
     * @param handle Worker handle
     * @return User context pointer
     */
    void* getContext(WorkerHandle_t handle);

    /**
     * @brief Get worker name
     * @param handle Worker handle
     * @return Worker name string
     */
    const char* getName(WorkerHandle_t handle);

    /**
     * @brief Check if worker is currently active
     * @param handle Worker handle
     * @retval true if worker is active (started and not stopped)
     */
    bool isActive(WorkerHandle_t handle);

    /**
     * @brief Get remaining time until next execution
     * @param handle Worker handle
     * @return Milliseconds until next execution (0 if ready, UINT32_MAX if not active)
     */
    uint32_t getTimeUntilNext(WorkerHandle_t handle);

    /**
     * @brief Get worker's minimum required stack size
     * @param handle Worker handle
     * @return Minimum stack size in bytes
     */
    uint16_t getMinStackSize(WorkerHandle_t handle);

    /**
     * @brief Get actual stack size of thread executing this worker
     * @param handle Worker handle
     * @return Actual thread stack size in bytes (0 if worker not found)
     */
    uint16_t getActualStackSize(WorkerHandle_t handle);

    /**
     * @brief Get maximum available stack size from pool configuration
     * @return Maximum stack size in bytes across all configured threads
     *         Returns 0 if pool not initialized
     */
    uint16_t getMaxAvailableStack() const { return m_max_available_stack; }

private:
    WorkerPool() = default;
    ~WorkerPool() = default;
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    static constexpr uint8_t MAX_THREADS = 6;
    static constexpr uint8_t MAX_WORKERS = 32;

    enum class CommandType : uint8_t {
        START,
        STOP,
        RESET,
        CHANGE_PERIOD,
        DELETE,
        FORCE_EXECUTE
    };

    enum class CallbackType : uint8_t {
        FUNCTION,
        OBJECT
    };

    struct WorkerControlBlock {
        char name[16];

        CallbackType callback_type;
        union {
            WorkerCallback_t function;
            WorkerCallbackBase* object;
        } callback;

        void* context;
        uint32_t period_ms;
        uint32_t next_execution;
        WorkerMode mode;
        WorkerPriority priority;
        uint16_t min_stack_bytes;
        bool active;
        bool pending_delete;
    };

    struct Command {
        CommandType type;
        WorkerControlBlock* worker;
        uint32_t value;
    };

    struct WorkerThread {
        osThreadId_t thread_id;
        uint16_t stack_bytes;
        WorkerPriority priority;
        uint8_t thread_index;
    };

    bool m_initialized{false};
    osMessageQueueId_t m_command_queue{nullptr};
    WorkerThread m_threads[MAX_THREADS];
    uint8_t m_num_threads{0};
    WorkerControlBlock* m_workers[MAX_WORKERS];
    uint8_t m_worker_count{0};
    uint16_t m_max_available_stack{0};

    static void workerThreadFunc(void* argument);

    bool canThreadExecuteWorker(const WorkerThread& thread,
                               const WorkerControlBlock* worker);
    int8_t findBestThreadForWorker(const WorkerControlBlock* worker);

    bool processCommand(uint8_t thread_idx, uint32_t timeout_ms);
    uint32_t calculateTimeout(uint8_t thread_idx);
    void executeReadyWorkers(uint8_t thread_idx);
    void executeWorker(WorkerControlBlock* worker);
    bool postCommand(const Command& cmd);
    WorkerControlBlock* findWorker(WorkerHandle_t handle);
};

/* ========================================================================== */
/* Predefined Configurations                                                  */
/* ========================================================================== */

/**
 * @brief Default configuration: 6 threads (2 priorities x 3 stack sizes)
 *
 * Total RAM: ~12KB
 * - Normal: 1KB + 2KB + 4KB = 7KB
 * - High:   1KB + 2KB + 4KB = 5KB
 *
 * Use for full-featured applications with various worker requirements
 */
constexpr WorkerPoolConfig DEFAULT_WORKER_POOL_CONFIG = {
    .command_queue_length = 20,
    .threads = {
        /* Normal priority workers */
        {1024, WorkerPriority::NORMAL},  /* 1KB - simple sensors, flags */
        {2048, WorkerPriority::NORMAL},  /* 2KB - moderate processing */
        {4096, WorkerPriority::NORMAL},  /* 4KB - heavy tasks (GPS, files) */

        /* High priority workers */
        {1024, WorkerPriority::HIGH},    /* 1KB - simple time-critical */
        {2048, WorkerPriority::HIGH},    /* 2KB - heart rate with DSP */
        {4096, WorkerPriority::HIGH}     /* 4KB - heavy time-critical */
    },
    .num_threads = 6
};

/**
 * @brief Minimal configuration: 4 threads (2 priorities x 2 stack sizes)
 *
 * Total RAM: ~6KB
 * - Normal: 1KB + 4KB = 5KB
 * - High:   1KB = 1KB
 *
 * Use for memory-constrained applications
 */
constexpr WorkerPoolConfig MINIMAL_WORKER_POOL_CONFIG = {
    .command_queue_length = 15,
    .threads = {
        {1024, WorkerPriority::NORMAL},  /* 1KB - simple tasks */
        {4096, WorkerPriority::NORMAL},  /* 4KB - heavy tasks */
        {1024, WorkerPriority::HIGH},    /* 1KB - time-critical */
        {2048, WorkerPriority::HIGH}     /* 2KB - heart rate */
    },
    .num_threads = 4
};

/**
 * @brief Compact configuration: 2 threads (only normal priority)
 *
 * Total RAM: ~3KB
 * - Normal: 1KB + 2KB = 3KB
 *
 * Use for simple applications without time-critical tasks
 */
constexpr WorkerPoolConfig COMPACT_WORKER_POOL_CONFIG = {
    .command_queue_length = 10,
    .threads = {
        {1024, WorkerPriority::NORMAL},  /* 1KB - simple tasks */
        {2048, WorkerPriority::NORMAL}   /* 2KB - moderate tasks */
    },
    .num_threads = 2
};

} /* namespace OS */
