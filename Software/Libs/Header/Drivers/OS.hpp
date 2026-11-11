/**
 ******************************************************************************
 * @file    OS.hpp
 * @brief   OS CPP wrappers.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __OS_HPP
#define __OS_HPP

#include <stdint.h>
#include <stdbool.h>
#include <variant>
#include <tuple>
#include <array>
#include <utility>
#include <cstdio>
#include <atomic>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "cmsis_os2.h"
#include "stream_buffer.h"

namespace OS {

inline void Delay(const uint32_t ms)
{
    osDelay(pdMS_TO_TICKS(ms));
}

inline bool DelayUntil(const uint32_t ms)
{
    return osDelayUntil(pdMS_TO_TICKS(ms)) == pdTRUE;
}

inline uint32_t GetTimeMs()
{
    return pdTICKS_TO_MS(osKernelGetTickCount());
}

inline void Yield()
{
    osThreadYield();
}

/**
 * @brief Base class for objects that can be interrupted
 *
 * Objects that block thread execution (queues, semaphores, etc.)
 * should inherit from this interface to support thread cancellation
 */
class ICancellable {
public:
    virtual ~ICancellable() = default;
    /**
     * @brief Wake up the blocked operation
     *
     * This method is called when thread termination is requested
     * to unblock any waiting operations
     */
    virtual void interrupt() = 0;
};
#if 0
/**
 * @brief Thread base class with cancellation support
 */
class __Thread {
public:
    enum class State {
        Created,    // Thread object created but not started
        Running,    // Thread is running
        Stopping,   // Stop requested
        Stopped     // Thread finished
    };

    __Thread() : mState(State::Created),
                 mInitialized(false),
                 mJoiningTask(nullptr),
                 mInitWaitingTask(nullptr) {
        for (size_t i = 0; i < MAX_CANCELLABLES; ++i) {
            mCancellables[i] = nullptr;
        }
    }

    /**
     * @brief Signal that thread initialization completed successfully
     */
    void setInitialized() {
        mInitialized.store(true);
        mState.store(State::Running);

        if (mInitWaitingTask != nullptr) {
            xTaskNotifyGive(mInitWaitingTask);
        }
    }

    /**
     * @brief Signal that thread initialization failed
     */
    void setInitFailed() {
        mInitialized.store(false);

        if (mInitWaitingTask != nullptr) {
            xTaskNotifyGive(mInitWaitingTask);
        }
    }

    /**
     * @brief Wait for thread to finish initialization
     * @param timeoutMs Timeout in milliseconds
     * @retval true Thread initialized successfully
     * @retval false Thread failed to initialize or timeout
     */
    bool waitInitialized(uint32_t timeoutMs) {
        // Check if already initialized (fast path)
        if (mInitialized.load(std::memory_order_acquire) &&
            mState.load(std::memory_order_acquire) == State::Running) {
            return true;
        }

        // Check if already failed (fast path)
        if (mState.load(std::memory_order_acquire) == State::Stopped &&
            !mInitialized.load(std::memory_order_acquire)) {
            return false;
        }

        // Set waiting task BEFORE thread starts
        mInitWaitingTask = xTaskGetCurrentTaskHandle();

        // Clear any pending notifications
        xTaskNotifyStateClear(nullptr);

        // Double-check after setting mInitWaitingTask
        if (mInitialized.load(std::memory_order_acquire) &&
            mState.load(std::memory_order_acquire) == State::Running) {
            mInitWaitingTask = nullptr;
            return true;
        }

        // Wait for notification from setInitialized() or setInitFailed()
        uint32_t notificationValue = 0;
        BaseType_t notifyResult = xTaskNotifyWait(
            0, 0xFFFFFFFF, &notificationValue,
            pdMS_TO_TICKS(timeoutMs)
        );

        mInitWaitingTask = nullptr;

        if (notifyResult != pdTRUE) {
            return false;  // Timeout
        }

        return mInitialized.load(std::memory_order_acquire) &&
               mState.load(std::memory_order_acquire) == State::Running;
    }

    /**
     * @brief Request thread termination
     */
    void requestStop() {
        if (mState.load() == State::Running) {
            mState.store(State::Stopping);

            for (size_t i = 0; i < MAX_CANCELLABLES; ++i) {
                if (mCancellables[i] != nullptr) {
                    mCancellables[i]->interrupt();
                }
            }
        }
    }

    /**
     * @brief Check if stop was requested
     * @retval true Stop was requested
     * @retval false Thread should continue running
     */
    bool shouldStop() const {
        return mState.load() == State::Stopping;
    }

    /**
     * @brief Wait for thread to finish (join)
     * @param timeoutMs Timeout in milliseconds
     * @retval true Thread finished successfully
     * @retval false Timeout occurred
     */
    bool join(uint32_t timeoutMs = portMAX_DELAY) {
        if (mState.load() == State::Stopped) {
            return true;
        }

        mJoiningTask = xTaskGetCurrentTaskHandle();

        uint32_t notificationValue = 0;
        BaseType_t result = xTaskNotifyWait(
            0, 0xFFFFFFFF, &notificationValue,
            pdMS_TO_TICKS(timeoutMs)
        );

        mJoiningTask = nullptr;
        return result == pdTRUE && mState.load() == State::Stopped;
    }

    /**
     * @brief Get current thread state
     * @return Current state
     */
    State getState() const {
        return mState.load();
    }

    /**
     * @brief Check if thread was successfully initialized
     * @retval true Thread initialized successfully
     * @retval false Thread not initialized or failed
     */
    bool isInitialized() const {
        return mInitialized.load();
    }

protected:
    void notifyJoiner() {
        if (mJoiningTask != nullptr) {
            xTaskNotifyGive(mJoiningTask);
        }
    }

    void setState(State state) {
        mState.store(state);
    }

    void resetInitFlag() {
        mInitialized.store(false);
    }

private:
    static constexpr size_t MAX_CANCELLABLES = 4;

    std::atomic<State> mState;
    std::atomic<bool> mInitialized;
    TaskHandle_t mJoiningTask;
    TaskHandle_t mInitWaitingTask;
    ICancellable* mCancellables[MAX_CANCELLABLES];

    friend class CancellableGuard;

    template <typename T, size_t N>
    friend class Thread;

    void registerCancellable(ICancellable* cancellable) {
        for (size_t i = 0; i < MAX_CANCELLABLES; ++i) {
            if (mCancellables[i] == nullptr) {
                mCancellables[i] = cancellable;
                return;
            }
        }
    }

    void unregisterCancellable(ICancellable* cancellable) {
        for (size_t i = 0; i < MAX_CANCELLABLES; ++i) {
            if (mCancellables[i] == cancellable) {
                mCancellables[i] = nullptr;
                return;
            }
        }
    }
};
/**
 * @brief RAII helper for automatic cancellable object registration
 *
 * Registers a cancellable object with a thread on construction
 * and unregisters it on destruction
 */
class CancellableGuard {
public:
    CancellableGuard(__Thread& thread, ICancellable& cancellable)
        : mThread(thread), mCancellable(cancellable) {
        mThread.registerCancellable(&mCancellable);
    }

    ~CancellableGuard() {
        mThread.unregisterCancellable(&mCancellable);
    }

    CancellableGuard(const CancellableGuard&) = delete;
    CancellableGuard& operator=(const CancellableGuard&) = delete;

private:
    __Thread& mThread;
    ICancellable& mCancellable;
};
#endif


/**
 * @brief   EventFlags
 * @code{.cpp}
 *   // Example:
 *   class MyClass {
 *   public:
 *      MyClass() {
 *          mEventFlags.init("MyEventFlagsName");
 *          mEventFlags.set(1 << 3);
 *      }
 *   private:
 *      EventFlags mEventFlags;
 *   }
 * @endcode
 */
class EventFlags final {
public:
    EventFlags() = default;

    EventFlags(const char *name)
    {
        init(name);
    }

    ~EventFlags()
    {
        deinit();
    }

    bool init(const char *name)
    {
        const osEventFlagsAttr_t kAttr = {
           .name = name,
           .attr_bits = 0,
           .cb_mem = &mControlBlock,
           .cb_size = sizeof(mControlBlock)
        };

        if (mHandle == nullptr) {
            mHandle = osEventFlagsNew(&kAttr);
        }

        return mHandle != nullptr;
    }

    void deinit()
    {
        if (mHandle) {
            osEventFlagsDelete(mHandle);
            mHandle = nullptr;
        }
    }

    uint32_t set(uint32_t flags)
    {
        return osEventFlagsSet(mHandle, flags);
    }

    uint32_t clear(uint32_t flags)
    {
        return osEventFlagsClear(mHandle, flags);
    }

    uint32_t get()
    {
        return osEventFlagsGet(mHandle);
    }

    bool isSet(uint32_t flags)
    {
        return ((osEventFlagsGet(mHandle) & flags) != 0);
    }

    bool isSetAll(uint32_t flags)
    {
        return ((osEventFlagsGet(mHandle) & flags) == flags);
    }

    uint32_t waitFlags(uint32_t flags, uint32_t options = osFlagsWaitAny,
            uint32_t timeout = osWaitForever)
    {
        return osEventFlagsWait(mHandle, flags, options, pdMS_TO_TICKS(timeout));
    }
    
    bool wait(uint32_t flags, uint32_t options = osFlagsWaitAny,
            uint32_t timeout = osWaitForever)
    {
        int32_t rflags = (int32_t)waitFlags(flags, options, timeout);
        if (rflags <= 0) {
            return false;
        }
        return (flags & rflags) != 0;
    }

    bool waitAnyNoClear(uint32_t flags, uint32_t timeout = osWaitForever) {
        int32_t rflags = (int32_t)waitFlags(flags, osFlagsWaitAny|osFlagsNoClear, timeout);
        if (rflags <= 0) {
            return false;
        }
        return (flags & rflags) != 0;
    }
    
    bool waitAllNoClear(uint32_t flags, uint32_t timeout = osWaitForever) {
        int32_t rflags = (int32_t)waitFlags(flags, osFlagsWaitAll|osFlagsNoClear, timeout);
        if (rflags <= 0) {
            return false;
        }
        return (flags & rflags) != flags;
    }

private:
    osEventFlagsId_t mHandle { };
    StaticEventGroup_t mControlBlock { };
};

/**
 * @brief   Mutex
 * @verbatim
 *   // Example:
 *   class MyClass {
 *   public:
 *      MyClass() {
 *          mMutex.init("MyMutexName");
 *          if (mMutex.lock(100) {
 *              // do something
 *              mMutex.release();
 *          }
 *      }
 *   private:
 *      Mutex mMutex;
 *   }
 * @endverbatim
 */
class Mutex final {
public:
    Mutex() = default;

    ~Mutex()
    {
        deinit();
    }

    bool init(const char *name, bool recursive = false)
    {
        const osMutexAttr_t kAttr = {
            .name = name,
            .attr_bits = recursive ? osMutexRecursive : 0,
            .cb_mem = &mControlBlock,
            .cb_size = sizeof(mControlBlock)
        };

        if (mHandle == nullptr) {
            mHandle = osMutexNew(&kAttr);
        }

        return mHandle != nullptr;
    }

    void deinit()
    {
        if (mHandle) {
            osMutexDelete (mHandle);
            mHandle = nullptr;
        }
    }

    bool lock(uint32_t timeout = osWaitForever)
    {
        return osMutexAcquire(mHandle, pdMS_TO_TICKS(timeout)) == osOK;
    }

    void release()
    {
        osMutexRelease(mHandle);
    }

private:
    osMutexId_t mHandle { };
    StaticSemaphore_t mControlBlock { };
};

/**
 * @brief RAII critical-section guard for a Mutex.
 *
 * Acquires the given @ref Mutex in the constructor (blocking with infinite timeout)
 * and releases it in the destructor. Use as an automatic local object to protect
 * a critical section even on early returns or exceptions.
 *
 * @code
 * void foo()
 * {
 *     MutexCS cs(mMutex);   // lock on construction
 *     // ... protected region ...
 * } // unlock on destruction
 * @endcode
 *
 * @note The referenced @ref Mutex must outlive this guard.
 * @see Mutex::lock(), Mutex::release()
 */
class MutexCS
{
public:

    /**
     * @brief Construct the guard and enter the critical section.
     *
     * Calls @ref Mutex::lock with infinite timeout (osWaitForever) on @p mutex.
     * This call blocks until the mutex is acquired.
     *
     * @param mutex Reference to the mutex to lock.
     */
    MutexCS(Mutex& mutex) : mMutex(mutex)
    {
        mMutex.lock(osWaitForever);
    }

    /**
     * @brief Destroy the guard and leave the critical section.
     *
     * Calls @ref Mutex::release on the underlying mutex.
     */
    ~MutexCS()
    {
        mMutex.release();
    }

    /// @name Non-copyable / non-movable
    /// Deleted to prevent multiple unlocks and dangling references.
    /// @{
    MutexCS(const MutexCS& other)            = delete;
    MutexCS& operator=(const MutexCS& other) = delete;
    MutexCS(MutexCS&&)                       = delete;
    MutexCS& operator=(MutexCS&&)            = delete;
    /// @}

private:
    Mutex& mMutex;
};

/**
 * @brief RAII guard for a Mutex.
 */
class MutexUnlocker
{
public:
    MutexUnlocker(Mutex& mutex) : mMutex(mutex)
    {}

    /**
     * @brief Destroy the guard and leave the critical section.
     *
     * Calls @ref Mutex::release on the underlying mutex.
     */
    ~MutexUnlocker()
    {
        mMutex.release();
    }

    /// @name Non-copyable / non-movable
    /// Deleted to prevent multiple unlocks and dangling references.
    /// @{
    MutexUnlocker(const MutexUnlocker& other)            = delete;
    MutexUnlocker& operator=(const MutexUnlocker& other) = delete;
    MutexUnlocker(MutexUnlocker&&)                       = delete;
    MutexUnlocker& operator=(MutexUnlocker&&)            = delete;
    /// @}

private:
    Mutex& mMutex;
};

/**
 * @brief Thin wrapper over FreeRTOS counting/binary semaphore.
 *
 * Use (max=1, init=0) for a binary "event" semaphore that wakes a task once.
 * Typical pattern: task waits with @ref take(), producers call @ref give() or
 * @ref giveFromISR() to wake it.
 *
 * @note This class uses dynamic allocation via xSemaphoreCreateCounting().
 *       For zero-heap systems, consider adding a Static variant
 *       with xSemaphoreCreateCountingStatic().
 */
class Semaphore : public ICancellable
{
public:
    /**
     * @brief Create a counting semaphore.
     *
     * @param max  Maximum count value (>0). For binary use 1.
     * @param init Initial count (<= max). For binary event use 0.
     */
    Semaphore(uint32_t max, uint32_t init) noexcept
        : mSemaphore(xSemaphoreCreateCounting(max, init))
    {
        assert (mSemaphore != nullptr);
    }

    /// @brief Destroy the semaphore and free RTOS resources.
    ~Semaphore() noexcept
    {
        if (mSemaphore) {
            vSemaphoreDelete(mSemaphore);
            mSemaphore = nullptr;
        }
    }

    /// @name Non-copyable / non-movable
    /// @{
    Semaphore(const Semaphore& other)            = delete;
    Semaphore& operator=(const Semaphore& other) = delete;
    Semaphore(Semaphore&&)                       = delete;
    Semaphore& operator=(Semaphore&&)            = delete;
    /// @}

    /**
     * @brief Take (decrement) the semaphore, optionally blocking.
     *
     * @param timeout Ticks to wait (default: @c portMAX_DELAY ).
     * @return true  if the semaphore was taken,
     * @return false if timeout expired.
     *
     * @note Do not call from ISR.
     */
    bool take(uint32_t timeout = portMAX_DELAY) noexcept
    {
        return (xSemaphoreTake(mSemaphore, pdMS_TO_TICKS(timeout)) == pdTRUE);
    }

    /**
     * @brief Give (increment) the semaphore from task context.
     *
     * @note For a binary semaphore (max=1), extra gives beyond 1 are ignored;
     *       we assert here to catch unintended flooding in debug builds.
     */
    bool give() noexcept
    {
        BaseType_t ok = xSemaphoreGive(mSemaphore);
        // In debug you may enforce this invariant:
        return ok == pdTRUE;
    }

    /**
     * @brief Give from ISR.
     *
     * @param higherPriorityTaskWoken (out) set to pdTRUE if a context switch
     *        should be requested at ISR exit.
     *
     * @code
     * void EXTI_IRQHandler()
     * {
     *     BaseType_t woken = pdFALSE;
     *     sem.giveFromISR(&woken);
     *     portYIELD_FROM_ISR(woken);
     * }
     * @endcode
     */
    void giveFromISR(BaseType_t* higherPriorityTaskWoken) noexcept
    {
        xSemaphoreGiveFromISR(mSemaphore, higherPriorityTaskWoken);
    }

    /**
     * @brief Interrupt blocked take operation
     *
     * Gives the semaphore to wake up waiting thread
     */
    void interrupt() override {
        if (mSemaphore != nullptr) {
            xSemaphoreGive(mSemaphore);
        }
    }

private:
    SemaphoreHandle_t mSemaphore;
};

/**
 * @brief   Queue
 * @verbatim
 *   // Example:
 *   class MyClass {
 *   public:
 *      MyClass() {
 *          mQueue.init("MyQueueName");
 *          MyStruct msg;
 *          mQueue.push(msg, 100);
 *      }
 *   private:
 *      struct MyStruct {
 *          int dummy;
 *      };
 *      Queue<MyStruct, 10> mQueue;
 *   }
 * @endverbatim
 */
template<typename T, size_t N>
class Queue : public ICancellable {
public:
    using ValueType = T;

    Queue() = default;

    ~Queue()
    {
        deinit();
    }

    bool init(const char *name)
    {
        const osMessageQueueAttr_t kAttr = {
            .name = name,
            .attr_bits = 0,
            .cb_mem = &mControlBlock,
            .cb_size = sizeof(mControlBlock),
            .mq_mem = mBuffer,
            .mq_size = sizeof(mBuffer),
        };

        if (mHandle == nullptr) {
            mHandle = osMessageQueueNew(N, sizeof(T), &kAttr);
        }

        return mHandle != nullptr;
    }

    void deinit()
    {
        if (mHandle) {
            osMessageQueueDelete(mHandle);
            mHandle = nullptr;
        }
    }

    size_t capacity() const
    {
        return N;
    }

    bool push(const T &item, uint32_t timeout = osWaitForever)
    {
        return osMessageQueuePut(mHandle, &item, 0, pdMS_TO_TICKS(timeout)) == osOK;
    }

    bool pop(T &item, uint32_t timeout = osWaitForever)
    {
        return osMessageQueueGet(mHandle, &item, 0, pdMS_TO_TICKS(timeout)) == osOK;
    }

    bool empty() const
    {
        return osMessageQueueGetCount(mHandle) == 0;
    }

    uint32_t size() const
    {
        return osMessageQueueGetCount(mHandle);
    }

    uint32_t available() const
    {
        return osMessageQueueGetSpace(mHandle);
    }

    void clear()
    {
        osMessageQueueReset(mHandle);
    }

    void interrupt() override {
        if (mHandle != nullptr) {
            T dummy{};
            push(dummy, 0);
        }
    }

    osMessageQueueId_t getHandle() const
    {
        return mHandle;
    }

private:
    osMessageQueueId_t mHandle { };
    StaticQueue_t mControlBlock { };
    T mBuffer[N] { };
};


template<typename T, size_t N>
class CircularBuffer {
public:
    using ValueType = T;

    CircularBuffer() = default;

    virtual ~CircularBuffer()
    {
        deinit();
    }

    bool init(const char *name)
    {
        mMutex = xSemaphoreCreateMutex();
        mNotEmpty = xSemaphoreCreateCounting(N, 0);
        mNotFull = xSemaphoreCreateCounting(N, N);
        return true;
    }

    void deinit()
    {
        if (mMutex) {
            vSemaphoreDelete(mMutex);
        }
        if (mNotEmpty) {
            vSemaphoreDelete(mNotEmpty);
        }
        if (mNotFull) {
            vSemaphoreDelete(mNotFull);
        }
    }

    size_t capacity() const
    {
        return N;
    }

    bool push(const T& item, uint32_t timeout = portMAX_DELAY)
    {
        if (xSemaphoreTake(mNotFull, pdMS_TO_TICKS(timeout)) == pdTRUE) {
            if (xSemaphoreTake(mMutex, portMAX_DELAY) == pdTRUE) {
                size_t tail = (mHead + mSize) % N;
                mBuffer[tail] = item;
                ++mSize;
                xSemaphoreGive(mMutex);
                xSemaphoreGive(mNotEmpty);
                return true;
            }
        }
        return false;
    }

    bool pop(T& item, uint32_t timeout = portMAX_DELAY)
    {
        if (xSemaphoreTake(mNotEmpty, pdMS_TO_TICKS(timeout)) == pdTRUE) {
            if (xSemaphoreTake(mMutex, portMAX_DELAY) == pdTRUE) {
                item = std::move(mBuffer[mHead]);
                mBuffer[mHead] = T{};

                mHead = (mHead + 1) % N;
                --mSize;
                xSemaphoreGive(mMutex);
                xSemaphoreGive(mNotFull);
                return true;
            }
        }
        return false;
    }

    bool empty() const
    {
        bool result = false;
        if (xSemaphoreTake(mMutex, portMAX_DELAY) == pdTRUE) {
            result = (mSize == 0);
            xSemaphoreGive(mMutex);
        }
        return result;
    }

    size_t size() const
    {
        size_t result = 0;
        if (xSemaphoreTake(mMutex, portMAX_DELAY) == pdTRUE) {
            result = mSize;
            xSemaphoreGive(mMutex);
        }
        return result;
    }

    uint32_t available() const
    {
        size_t result = 0;
        if (xSemaphoreTake(mMutex, portMAX_DELAY) == pdTRUE) {
            result = N - mSize;
            xSemaphoreGive(mMutex);
        }
        return result;
    }

    void clear()
    {
        if (xSemaphoreTake(mMutex, portMAX_DELAY) == pdTRUE) {
            for (size_t i = 0; i < N; ++i) {
                mBuffer[i] = T{};
            }
            mHead = 0;
            mSize = 0;
            xSemaphoreGive(mMutex);
        }
    }

    osMessageQueueId_t getHandle() const
    {
        return mNotEmpty;
    }

private:
    SemaphoreHandle_t mMutex {};
    SemaphoreHandle_t mNotEmpty {};
    SemaphoreHandle_t mNotFull {};
    std::array<T, N> mBuffer {};
    size_t mHead = 0;
    size_t mSize = 0;
};


#if ( configUSE_QUEUE_SETS == 1 )
/**
 * @brief   QueueSet
 * @verbatim
 *   // Example:
 *   class MyClass {
 *   public:
 *      MyClass() {
 *          mQueue1.init("MyQueueName1");
 *          mQueue2.init("MyQueueName2");
 *          mThread.init("MyThreadName", this, &MyClass::thread, osPriorityNormal);
 *      }
 *   private:
 *      struct MyStruct1 {
 *          int dummy1;
 *      };
 *      struct MyStruct2 {
 *          int dummy2;
 *      };
 *      Queue<MyStruct1, 10> mQueue1;
 *      Queue<MyStruct2, 10> mQueue2;
 *      Thread<MyClass, 2048> mThread;
 *
 *      void handler(MyStruct1 data){
 *          printf("%d\n", value.dummy1);
 *      }
 *      void handler(MyStruct2 data){
 *          printf("%d\n", value.dummy2);
 *      }
 *
 *      void thread() {
 *
 *          QueueSet queueSet(mQueue1, mQueue2);
 *          for(;;) {
 *              auto result = queueSet.wait(1000);
 *
 *              std::visit([this](auto &&value) {
 *                  using T = std::decay_t<decltype(value)>;
 *                  if constexpr (std::is_same_v<T, std::monostate>) {
 *                      // Timeout
 *                  } else {
 *                      this->handler(value);
 *                  }
 *              }, result);
 *          }
 *      }
 *
 *   }
 * @endverbatim
 */
template<typename... Queues>
class QueueSet {
public:

    explicit QueueSet(Queues&... queues)
        : mQueues(queues...) {
        // Calculate the total number of places in the queues
        const size_t totalQueueLength = (queues.capacity() + ...);
        mQueueSetHandle = xQueueCreateSet(totalQueueLength);

        // Add each queue to the Queue Set
        (xQueueAddToSet(static_cast<QueueSetMemberHandle_t>(queues.getHandle()), mQueueSetHandle), ...);
    }

    // Waiting for a message from any queue.
    std::variant<std::monostate, typename Queues::ValueType...> wait(uint32_t timeout = osWaitForever) {
        QueueSetMemberHandle_t activatedHandle = xQueueSelectFromSet(mQueueSetHandle, timeout);
        if (activatedHandle == nullptr) {
            // Return an empty std::variant in case of timeout
            return std::monostate{};
        }

        return receiveFromQueue(activatedHandle);
    }

private:
    // Recursive function to get data from the desired queue
    template<size_t Index = 0>
    auto receiveFromQueue(QueueSetMemberHandle_t handle) -> std::variant<std::monostate, typename Queues::ValueType...> {
        if (std::get<Index>(mQueues).getHandle() == handle) {
            using QueueType = std::tuple_element_t<Index, std::tuple<Queues...>>;
            typename QueueType::ValueType value;
            std::get<Index>(mQueues).pop(value, 0);
            return value;
        } else {
            if constexpr (Index + 1 < sizeof...(Queues)) {
                return receiveFromQueue<Index + 1>(handle);
            } else {
                // This shouldn't happen, but in case of an error
                return std::monostate{};
            }
        }
    }

    std::tuple<Queues&...> mQueues;
    QueueSetHandle_t mQueueSetHandle;
};
#endif /* configUSE_QUEUE_SETS */

/**
 * @brief   TimerStatic
 * @brief   For use outside of class with static callback
 * @verbatim
 *   // Example:
 *   TimerStatic mTimer;
 *
 *   void callback(const void *arg) {
 *      printf("Callback. arg = 0x%08X\n", arg);
 *   }
 *
 *   mTimer.init("MyTimerName", &callback, true, nullptr);
 *   mTimer.start(5000);
 *
 * @endverbatim
 */
class TimerStatic final {
public:

    TimerStatic() = default;

    ~TimerStatic()
    {
        deinit();
    }

    bool init(const char *name, osTimerFunc_t cb, bool periodic = false,
            void *argument = nullptr)
    {
        const osTimerAttr_t kAttr = {
             .name = name,
             .attr_bits = 0,
             .cb_mem = &mControlBlock,
             .cb_size = sizeof(mControlBlock),
         };

        if (mHandle == nullptr) {
            mHandle = osTimerNew(cb,
                    periodic ? osTimerPeriodic : osTimerOnce, (void*)argument, &kAttr);
        }

        return mHandle != nullptr;
    }

    void deinit()
    {
        if (mHandle) {
            osTimerDelete(mHandle);
            mHandle = nullptr;
        }
    }

    bool start(uint32_t ms)
    {
        return osTimerStart(mHandle, pdMS_TO_TICKS(ms)) == osOK;
    }

    bool stop()
    {
        return osTimerStop(mHandle) == osOK;
    }

    bool isRunning()
    {
        return osTimerIsRunning(mHandle);
    }

private:
    osTimerId_t mHandle { };
    StaticTimer_t mControlBlock { };
};

/**
 * @brief   Timer
 * @verbatim
 *   // Example:
 *   class MyClass {
 *   public:
 *      MyClass() {
 *          mTimer.init("MyTimerName", this, &MyClass::callback, true, this);
 *          mTimer.start(5000);
 *      }
 *   private:
 *      Timer<MyClass> mTimer;
 *
 *      void callback(void *arg) {
 *          printf("MyClass callback. arg = 0x%08X\n", arg);
 *      }
 *   }
 * @endverbatim
 */
template <typename T>
class Timer final {
public:

    /// Timer callback function.
    typedef void (T::*CallbackFunc_t) (void *);

    Timer() = default;

    ~Timer()
    {
        deinit();
    }

    bool init(const char *name, T *instance, CallbackFunc_t callback, bool periodic = false,
            void *argument = nullptr)
    {
        if (name == nullptr || callback == nullptr || instance == nullptr) {
            return false;
        }
        mpCallerInstance = instance;
        mCallbackFunc = callback;
        mArgument = argument;

        const osTimerAttr_t kAttr = {
             .name = name,
             .attr_bits = 0,
             .cb_mem = nullptr,
             .cb_size = 0,
         };

        if (mHandle == nullptr) {
            mHandle = osTimerNew(RtosCallback,
                    periodic ? osTimerPeriodic : osTimerOnce, this, &kAttr);
        }

        return mHandle != nullptr;
    }

    void setCallback(CallbackFunc_t callback)
    {
        mCallbackFunc = callback;
    }

    void deinit()
    {
        if (mHandle) {
            osTimerDelete(mHandle);
            mHandle = nullptr;
        }
    }

    bool start(uint32_t ms)
    {
        return osTimerStart(mHandle, pdMS_TO_TICKS(ms)) == osOK;
    }

    bool stop()
    {
        return osTimerStop(mHandle) == osOK;
    }

    bool isRunning()
    {
        return osTimerIsRunning(mHandle);
    }

private:
    osTimerId_t mHandle { };
    CallbackFunc_t mCallbackFunc { };
    T *mpCallerInstance { };
    void *mArgument { };

    static void RtosCallback(void *argument)
    {
        Timer *pInstance = static_cast<Timer*>(argument);
        (pInstance->mpCallerInstance->*pInstance->mCallbackFunc)(pInstance->mArgument);
    }
};
#if 0
/**
 * @brief   Thread
 * @verbatim
 *   // Example:
 *   class MyClass {
 *   public:
 *      MyClass() {
 *          mThread.init("MyThreadName", this, &MyClass::thread, osPriorityNormal);
 *      }
 *   private:
 *      Thread<MyClass, 2048> mThread;
 *
 *      void thread() {
 *          for(;;) {
 *              printf("Alive\n");
 *              osDelay(1000);
 *          }
 *      }
 *   }
 * @endverbatim
 *
 * @param   T: Class type
 * @param   N: Stack size in bytes
 */
template <typename T, size_t N>
class Thread : public __Thread {
public:

    /// Thread function.
    typedef void (T::*ThreadFunc_t) ();

    Thread() = default;

    ~Thread()
    {
        deinit();
    }

    bool init(const char *name, T *instance, ThreadFunc_t thread, osPriority_t prio =
            osPriorityNormal)
    {
        if (name == nullptr || thread == nullptr || instance == nullptr) {
            return false;
        }

        mpCallerInstance = instance;
        mThreadFunc = thread;

        const osThreadAttr_t kAttr = {
            .name = name,
            .cb_mem = &mControlBlock,
            .cb_size = sizeof(mControlBlock),
            .stack_mem = &mBuffer[0],
            .stack_size = sizeof(mBuffer),
            .priority = prio
        };

        if (mHandle == nullptr) {
            mHandle = osThreadNew(RtosThread, this, &kAttr);
        }

        return mHandle != nullptr;
    }

    /**
     * @brief Initialize and start thread with initialization timeout
     * @param name Thread name
     * @param instance Class instance pointer
     * @param thread Thread function pointer
     * @param prio Thread priority
     * @param initTimeoutMs Timeout for thread initialization in milliseconds
     * @retval true Thread created and initialized successfully
     * @retval false Thread creation failed or initialization timeout
     */
    bool init(const char *name, T *instance, ThreadFunc_t thread,
              osPriority_t prio, uint32_t initTimeoutMs)
    {
        if (name == nullptr || thread == nullptr || instance == nullptr) {
            return false;
        }

        // Check if already initialized or in use
        State currentState = mState.load(std::memory_order_acquire);
        if (mHandle != nullptr ||
            (currentState != State::Created && currentState != State::Stopped)) {
            return false;
        }

        // Reset state for clean initialization
        resetInitFlag();
        setState(State::Created);

        mpCallerInstance = instance;
        mThreadFunc = thread;

        const osThreadAttr_t kAttr = {
            .name = name,
            .cb_mem = &mControlBlock,
            .cb_size = sizeof(mControlBlock),
            .stack_mem = &mBuffer[0],
            .stack_size = sizeof(mBuffer),
            .priority = prio
        };

        // Create thread
        mHandle = osThreadNew(RtosThread, this, &kAttr);
        if (mHandle == nullptr) {
            return false;
        }

        // Wait for initialization to complete
        if (!waitInitialized(initTimeoutMs)) {
            // Initialization failed or timeout
            requestStop();

            // Try to join gracefully
            if (!join(1000)) {
                // Thread didn't respond to stop request
                // Force termination (last resort)
            }

            deinit();
            return false;
        }

        return true;
    }

    void deinit()
    {
        if (mHandle) {
            osThreadTerminate(mHandle);
            mHandle = nullptr;
        }
    }

    osThreadId_t getId()
    {
        return mHandle;
    }

    uint32_t getStackSpace()
    {
        return osThreadGetStackSpace(mHandle);
    }

    bool suspend()
    {
        return osThreadSuspend(mHandle) == osOK;
    }

    bool resume()
    {
        return osThreadResume(mHandle) == osOK;
    }


#if (configUSE_OS2_THREAD_FLAGS == 1)
    uint32_t flagsSet(uint32_t flags)
    {
        return osThreadFlagsSet(mHandle, flags);
    }

    uint32_t flagsClear(uint32_t flags)
    {
        if (xTaskGetCurrentTaskHandle() != mHandle) {
            return (uint32_t)osError;
        }
        return osThreadFlagsClear(flags);
    }

    uint32_t flagsGet()
    {
        if (xTaskGetCurrentTaskHandle() != mHandle) {
            return (uint32_t)osError;
        }
        return osThreadFlagsGet();
    }

    bool flagsIsSet(uint32_t flags)
    {
        if (xTaskGetCurrentTaskHandle() != mHandle) {
            return false;
        }
        return (osThreadFlagsGet() & flags) != 0;
    }

    uint32_t flagsWait(uint32_t flags, uint32_t options = osFlagsWaitAny, 
            uint32_t timeout = osWaitForever)
    {
        if (xTaskGetCurrentTaskHandle() != mHandle) {
            return (uint32_t) osError;
        }

        return osThreadFlagsWait(flags, options, pdMS_TO_TICKS(timeout));
    }
    
    bool flagsWaitAnyNoClear(uint32_t flags, uint32_t timeout = osWaitForever)
    {
        int32_t rflags = (int32_t) flagsWait(flags,
                osFlagsWaitAny | osFlagsNoClear, timeout);
        if (rflags <= 0) {
            return false;
        }
        return (flags & rflags) != 0;
    }
       
    bool flagsWaitAllNoClear(uint32_t flags, uint32_t timeout = osWaitForever)
    {
        int32_t rflags = (int32_t) flagsWait(flags,
                osFlagsWaitAll | osFlagsNoClear, timeout);
        if (rflags <= 0) {
            return false;
        }
        return (flags & rflags) != flags;
    }
#endif

private:
    osThreadId_t mHandle { };
    StaticTask_t mControlBlock { };
    uint8_t mBuffer[N] { };

    ThreadFunc_t mThreadFunc { };
    T *mpCallerInstance { };

    /**
     * @brief FreeRTOS thread entry point
     * @param argument Pointer to Thread instance
     */
    static void RtosThread(void *argument) {
        Thread *pInstance = static_cast<Thread*>(argument);
        pInstance->mHandle = xTaskGetCurrentTaskHandle();

        // User thread function - will call setInitialized() or setInitFailed()
        (pInstance->mpCallerInstance->*pInstance->mThreadFunc)();

        // Set stopped state and notify joiner
        pInstance->setState(State::Stopped);
        pInstance->notifyJoiner();

        osThreadExit();
    }

};
#endif
/**
 * @brief StreamBuffer wrapper with interrupt support
 */
template <size_t N>
class StreamBuffer : public ICancellable {
public:
    StreamBuffer() = default;

    ~StreamBuffer()
    {
        deinit();
    }

    bool init(const char *name, uint32_t triggerLevel = 1)
    {
        if (name == nullptr) {
            return false;
        }

        if (mHandle == nullptr) {
            mHandle = xStreamBufferCreateStatic(sizeof(mBuffer), triggerLevel, mBuffer,
                    &mControlBlock);
        }

        return mHandle != nullptr;
    }

    void deinit()
    {
        if (mHandle) {
            vStreamBufferDelete(mHandle);
            mHandle = nullptr;
        }
    }

    uint32_t sendFromISR(const uint8_t *pData, uint32_t length)
    {
        return xStreamBufferSendFromISR(mHandle, (void*) pData, length, nullptr);
    }

    uint32_t send(const uint8_t *pData, uint32_t length, uint32_t timeout =
            osWaitForever)
    {
        return xStreamBufferSend(mHandle, (void*) pData, length, pdMS_TO_TICKS(timeout));
    }

    uint32_t receiveFromISR(uint8_t *pData, uint32_t length)
    {
        return xStreamBufferReceiveFromISR(mHandle, (void*) pData, length,
                nullptr);
    }

    uint32_t receive(uint8_t *pData, uint32_t length, uint32_t timeout =
            osWaitForever)
    {
        return xStreamBufferReceive(mHandle, (void*) pData, length, pdMS_TO_TICKS(timeout));
    }

    uint32_t getCount()
    {
        return xStreamBufferBytesAvailable(mHandle);
    }

    uint32_t getFreeSpace()
    {
        return xStreamBufferSpacesAvailable(mHandle);
    }

    void clear()
    {
        xStreamBufferReset(mHandle);
    }

    /**
     * @brief Interrupt blocked receive operation
     *
     * Sends a dummy byte to wake up waiting thread
     */
    void interrupt() override {
        if (mHandle != nullptr) {
            uint8_t dummy = 0;
            xStreamBufferSend(mHandle, &dummy, 1, 0);
        }
    }

    StreamBufferHandle_t getHandle() const {
        return mHandle;
    }

private:
    StreamBufferHandle_t mHandle { };
    StaticStreamBuffer_t mControlBlock { };
    uint8_t mBuffer[N] { };
};

//-----------------------------------

//-----------------------------------




/**
 * @brief Thread base class with safe initialization
 */
class __Thread {
public:
    enum class State : uint8_t {
        Idle = 0,    // ready for init()
        Starting,    // init in progress (thread created, waiting for setInitialized()/setInitFailed())
        Running,     // successfully initialized
        Stopping,    // stop requested (thread should exit)
        Stopped,     // thread finished execution
        Failed       // init failed
    };

    __Thread()
        : mState(State::Idle),
          mInitialized(false),
          mAutoInit(false),
          mHandle(nullptr),
          mJoiningTask(nullptr),
          mInitWaitingTask(nullptr) {
        for (size_t i = 0; i < MAX_CANCELLABLES; ++i) {
            mCancellables[i] = nullptr;
        }
    }

    virtual ~__Thread() = default;

    /**
     * @brief Signal that thread initialization completed successfully
     * @note MUST be called from thread function after successful initialization
     */
    void setInitialized() {
        // Publish side-effects (mInitialized) BEFORE we publish the Running state.
        // Use relaxed -> then release on state to form acquire-release pair.
        mInitialized.store(true, std::memory_order_relaxed);
        State expected = State::Starting;
        if (mState.compare_exchange_strong(expected, State::Running,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
            // Load waiting task and notify it
            TaskHandle_t waiting = mInitWaitingTask.load(std::memory_order_acquire);
            if (waiting != nullptr) {
                xTaskNotifyGive(waiting);
            }
        }
    }

    /**
     * @brief Signal that thread initialization failed
     * @note MUST be called from thread function if initialization fails
     */
    void setInitFailed() {
        // Set initialized = false as a side-effect before publishing failure
        mInitialized.store(false, std::memory_order_relaxed);
        State expected = State::Starting;
        if (mState.compare_exchange_strong(expected, State::Failed,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
            TaskHandle_t waiting = mInitWaitingTask.load(std::memory_order_acquire);
            if (waiting != nullptr) {
                xTaskNotifyGive(waiting);
            }
        }
    }

    /**
     * @brief Get current thread state
     * @return Current state
     */
    State getState() const {
        return mState.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if thread was successfully initialized
     * @retval true Thread initialized and running
     * @retval false Thread not initialized or failed
     */
    bool isInitialized() const {
        // After setInitialized() the state Running is published with release,
        // so acquiring load of state ensures visibility of prior mInitialized write.
        return (mState.load(std::memory_order_acquire) == State::Running) &&
               mInitialized.load(std::memory_order_acquire);
    }

    /**
     * @brief Register a cancellable object for interruption on thread stop
     * @param cancellable Pointer to ICancellable object
     * @retval true Successfully registered
     * @retval false Registration failed (no free slots)
     * @note Thread-safe: can be called from the thread itself
     */
    bool registerCancellable(ICancellable* cancellable) {
        if (cancellable == nullptr) {
            return false;
        }
        for (size_t i = 0; i < MAX_CANCELLABLES; ++i) {
            if (mCancellables[i] == nullptr) {
                mCancellables[i] = cancellable;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Unregister a cancellable object
     * @param cancellable Pointer to ICancellable object
     * @retval true Successfully unregistered
     * @retval false Object was not registered
     * @note Thread-safe: can be called from the thread itself
     */
    bool unregisterCancellable(ICancellable* cancellable) {
        if (cancellable == nullptr) {
            return false;
        }
        for (size_t i = 0; i < MAX_CANCELLABLES; ++i) {
            if (mCancellables[i] == cancellable) {
                mCancellables[i] = nullptr;
                return true;
            }
        }
        return false;
    }

protected:
    /**
     * @brief Wait for thread initialization to complete
     * @param timeoutMs Timeout in milliseconds
     * @retval true Thread initialized successfully
     * @retval false Initialization failed or timeout
     */
    bool waitInitialized(uint32_t timeoutMs) {
        // Fast-path checks
        State st = mState.load(std::memory_order_acquire);
        if (st == State::Running && mInitialized.load(std::memory_order_acquire)) {
            return true;
        }
        if (st == State::Failed || st == State::Stopped) {
            return false;
        }
        if (st != State::Starting) {
            // Not in starting state: invalid call or raced away
            return false;
        }

        // Get current task handle and clear any pending notifications first.
        // CLEAR first to avoid the classic lost-notify race.
        TaskHandle_t cur = xTaskGetCurrentTaskHandle();
        xTaskNotifyStateClear(nullptr); // clear notifications for current task

        // Store waiting handle atomically (release)
        mInitWaitingTask.store(cur, std::memory_order_release);

        // Double-check after setting waiting handle (close race window)
        st = mState.load(std::memory_order_acquire);
        if (st == State::Running && mInitialized.load(std::memory_order_acquire)) {
            mInitWaitingTask.store(nullptr, std::memory_order_release);
            return true;
        }
        if (st == State::Failed || st == State::Stopped) {
            mInitWaitingTask.store(nullptr, std::memory_order_release);
            return false;
        }

        // Wait for notification
        uint32_t notificationValue = 0;
        BaseType_t notifyResult = xTaskNotifyWait(
            0,            // clear bits on entry mask
            0xFFFFFFFFu,  // clear bits on exit mask
            &notificationValue,
            pdMS_TO_TICKS(timeoutMs)
        );

        // Clear waiting handle
        mInitWaitingTask.store(nullptr, std::memory_order_release);

        if (notifyResult != pdTRUE) {
            // Timeout or error
            return false;
        }

        // Final verification
        return (mState.load(std::memory_order_acquire) == State::Running) &&
               mInitialized.load(std::memory_order_acquire);
    }

    /**
     * @brief Notify joining task that thread has finished
     */
    void notifyJoiner() {
        TaskHandle_t joiner = mJoiningTask.load(std::memory_order_acquire);
        if (joiner != nullptr) {
            xTaskNotifyGive(joiner);
        }
    }

    /**
     * @brief Set thread state (protected)
     * @param state New state
     */
    void setState(State state) {
        mState.store(state, std::memory_order_release);
    }

    /**
     * @brief Transition to Stopped state and notify joiner
     * @note Valid transitions to Stopped: Running, Stopping, Failed
     */
    void markStopped() {
        State cur = mState.load(std::memory_order_acquire);
        if (cur == State::Running || cur == State::Stopping || cur == State::Failed) {
            mState.store(State::Stopped, std::memory_order_release);
            notifyJoiner();
        }
    }

    /**
     * @brief Interrupt all registered cancellable objects
     * @note Called by derived class when thread stop is requested
     */
    void interruptCancellables() {
        for (size_t i = 0; i < MAX_CANCELLABLES; ++i) {
            if (mCancellables[i] != nullptr) {
                mCancellables[i]->interrupt();
            }
        }
    }

    // Members (atomics where cross-thread access occurs)
    std::atomic<State> mState;
    std::atomic<bool> mInitialized;
    std::atomic<bool> mAutoInit;  // true if user wants auto-initialization (timeout=0)
    // Thread handle and task handles must be atomic to avoid races on read/write
    std::atomic<osThreadId_t> mHandle;
    std::atomic<TaskHandle_t> mJoiningTask;
    std::atomic<TaskHandle_t> mInitWaitingTask;

    // Cancellable objects support
    static constexpr size_t MAX_CANCELLABLES = 4;
    ICancellable* mCancellables[MAX_CANCELLABLES];

    // Control block / stack area are owned by derived template
    template <typename T, size_t N>
    friend class Thread;
};

/**
 * @brief Thread with safe initialization and lifecycle management
 * @verbatim
 *   // Example 1: Auto-initialization mode (timeout=0, default)
 *   class SimpleClass {
 *   public:
 *      SimpleClass() {
 *          // Thread auto-initializes when it starts - no need to call setInitialized()
 *          if (!mThread.init("SimpleThread", this, &SimpleClass::threadFunc)) {
 *              // Initialization failed
 *          }
 *          // Thread is now running (or will be very soon)
 *      }
 *
 *      ~SimpleClass() {
 *          mThread.deinit();  // Safe cleanup
 *      }
 *
 *   private:
 *      Thread2<SimpleClass, 2048> mThread;
 *
 *      void threadFunc() {
 *          // No need to call setInitialized() in auto-init mode
 *          // Main loop
 *          while (!shouldStop()) {
 *              // Work...
 *              osDelay(100);
 *          }
 *      }
 *   };
 *
 *   // Example 2: Manual initialization mode (timeout>0)
 *   class ComplexClass {
 *   public:
 *      ComplexClass() {
 *          // Wait up to 2000ms for manual initialization
 *          if (!mThread.init("ComplexThread", this, &ComplexClass::threadFunc,
 *                           osPriorityNormal, 2000)) {
 *              // Initialization failed or timeout
 *          }
 *          // Thread is initialized and running
 *      }
 *
 *      ~ComplexClass() {
 *          mThread.deinit();  // Safe cleanup
 *      }
 *
 *   private:
 *      Thread2<ComplexClass, 2048> mThread;
 *
 *      void threadFunc() {
 *          // Perform complex initialization
 *          if (!setupHardware()) {
 *              setInitFailed();  // Signal failure and return
 *              return;
 *          }
 *
 *          setInitialized();  // Signal success - REQUIRED in manual mode
 *
 *          // Main loop
 *          while (!shouldStop()) {
 *              // Work...
 *              osDelay(100);
 *          }
 *      }
 *   };
 *
 *   // Example 3: Using cancellable objects for graceful shutdown
 *   class QueueProcessor {
 *   public:
 *      QueueProcessor() {
 *          if (!mThread.init("Processor", this, &QueueProcessor::threadFunc)) {
 *              // Initialization failed
 *          }
 *      }
 *
 *      ~QueueProcessor() {
 *          mThread.deinit();  // requestStop() will interrupt queue operations
 *      }
 *
 *   private:
 *      Thread2<QueueProcessor, 2048> mThread;
 *      Queue<Message> mQueue;  // Inherits from ICancellable
 *
 *      void threadFunc() {
 *          // Auto-register queue for interruption on thread stop
 *          CancellableGuard guard(&mThread, &mQueue);
 *
 *          while (!shouldStop()) {
 *              Message msg;
 *              // This will be interrupted when deinit() calls requestStop()
 *              if (mQueue.receive(msg, osWaitForever)) {
 *                  processMessage(msg);
 *              }
 *          }
 *      }
 *   };
 * @endverbatim
 *
 * @param T Class type
 * @param N Stack size in bytes
 */
template <typename T, size_t N>
class Thread : public __Thread {
public:
    typedef void (T::*ThreadFunc_t)();

    Thread() = default;

    ~Thread() {
        deinit();
    }

    /**
     * @brief Initialize and start thread with initialization timeout
     * @param name Thread name
     * @param instance Class instance pointer
     * @param thread Thread function pointer
     * @param prio Thread priority
     * @param initTimeoutMs Timeout for thread initialization in milliseconds.
     *                      Use 0 (default) for auto-initialization: thread is automatically
     *                      marked as initialized when it starts (no need to call
     *                      setInitialized() in user code). init() will wait up to 1 second
     *                      for thread to start (usually takes only a few milliseconds).
     *                      Use non-zero timeout to wait for explicit setInitialized() call
     *                      from user code after custom initialization logic.
     * @retval true Thread created and initialized successfully
     * @retval false Thread creation failed or initialization timeout
     * @note With timeout=0 (auto-init), init() guarantees thread has started executing.
     *       With timeout>0 (manual init), user MUST call setInitialized() or setInitFailed().
     * @note On failure, thread returns to Idle state and can be re-initialized
     */
    bool init(const char *name, T *instance, ThreadFunc_t thread,
              osPriority_t prio = osPriorityNormal,
              uint32_t initTimeoutMs = 0) {
        if (name == nullptr || instance == nullptr || thread == nullptr) {
            return false;
        }

        // Only allow init from Idle
        State cur = mState.load(std::memory_order_acquire);
        if (cur != State::Idle) {
            return false;
        }

        // Transition Idle -> Starting (atomically)
        State expected = State::Idle;
        if (!mState.compare_exchange_strong(expected, State::Starting,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
            return false;
        }

        // Save parameters
        mpCallerInstance = instance;
        mThreadFunc = thread;

        // Reset init flag (side-effect)
        mInitialized.store(false, std::memory_order_relaxed);

        // Set auto-init flag if timeout is 0
        mAutoInit.store(initTimeoutMs == 0, std::memory_order_relaxed);

        // Prepare thread attributes (user supplies stack buffer & control block)
        const osThreadAttr_t kAttr = {
            .name = name,
            .cb_mem = &mControlBlock,
            .cb_size = sizeof(mControlBlock),
            .stack_mem = &mBuffer[0],
            .stack_size = sizeof(mBuffer),
            .priority = prio
        };

        // Create thread. rtosThreadEntry will set mHandle (atomic) as first action.
        osThreadId_t created = osThreadNew(&Thread::rtosThreadEntry, this, &kAttr);
        if (created == nullptr) {
            // Creation failed -> rollback to Idle
            mState.store(State::Idle, std::memory_order_release);
            mpCallerInstance = nullptr;
            mThreadFunc = nullptr;
            mAutoInit.store(false, std::memory_order_relaxed);
            return false;
        }

        // Store handle returned by osThreadNew
        // Note: rtosThreadEntry will also store xTaskGetCurrentTaskHandle()
        // In FreeRTOS these are the same, so double-store is harmless
        mHandle.store(created, std::memory_order_release);

        // Always wait for initialization confirmation
        // If timeout=0 (auto-init), rtosThreadEntry will call setInitialized() immediately
        // when thread starts, so this wait will be very short (few milliseconds max)
        // If timeout>0 (manual init), user must call setInitialized() explicitly
        uint32_t actualTimeout = (initTimeoutMs == 0) ? 1000 : initTimeoutMs;
        bool initSuccess = waitInitialized(actualTimeout);

        if (!initSuccess) {
            // Cleanup and return to Idle
            cleanupFailedInit();
            return false;
        }

        return true;
    }

    /**
     * @brief Deinitialize thread and free resources
     * @note Thread must be stopped before calling deinit
     * @note After deinit, thread returns to Idle state and can be re-initialized
     */
    void deinit() {
        State cur = mState.load(std::memory_order_acquire);
        if (cur == State::Idle) {
            return;
        }

        // If Running or Starting, request stop and wait join
        if (cur == State::Running || cur == State::Starting) {
            requestStop();
            join(5000); // Wait up to 5s
        }

        // Delete the task. A gracefully stopped thread parks suspended after
        // its function returns (see rtosThreadEntry), so this terminate is the
        // one and only delete -- and because it targets another task, the TCB
        // is unlinked from the scheduler synchronously, before our owner's
        // memory can be freed. (If join() timed out this still force-kills a
        // live task, as it always did -- resources it holds are orphaned.)
        if (!terminateTask()) {
            // Task not deleted: stay out of Idle so the control block can
            // never be re-initialized or handed back while the scheduler may
            // still reference it.
            return;
        }

        // Reset members and return to Idle
        mpCallerInstance = nullptr;
        mThreadFunc = nullptr;
        mInitialized.store(false, std::memory_order_release);
        mAutoInit.store(false, std::memory_order_release);
        mState.store(State::Idle, std::memory_order_release);
    }

    /**
     * @brief Wait for thread to finish execution
     * @param timeoutMs Timeout in milliseconds
     * @retval true Thread finished successfully
     * @retval false Timeout occurred
     */
    bool join(uint32_t timeoutMs = portMAX_DELAY) {
        State cur = mState.load(std::memory_order_acquire);

        // Already stopped or idle
        if (cur == State::Stopped || cur == State::Idle) {
            return true;
        }

        // Join only valid from Running or Stopping (or Failed)
        if (cur != State::Running && cur != State::Stopping && cur != State::Failed) {
            return false;
        }

        // Clear pending notifications for current task BEFORE storing joining handle
        xTaskNotifyStateClear(nullptr);
        mJoiningTask.store(xTaskGetCurrentTaskHandle(), std::memory_order_release);

        // Double-check state
        cur = mState.load(std::memory_order_acquire);
        if (cur == State::Stopped || cur == State::Idle) {
            mJoiningTask.store(nullptr, std::memory_order_release);
            return true;
        }

        // Wait for notification that thread stopped
        uint32_t notificationValue = 0;
        BaseType_t result = xTaskNotifyWait(
            0, 0xFFFFFFFFu, &notificationValue,
            pdMS_TO_TICKS(timeoutMs)
        );

        mJoiningTask.store(nullptr, std::memory_order_release);

        if (result != pdTRUE) {
            // Timeout
            return false;
        }

        // Confirm final state
        return (mState.load(std::memory_order_acquire) == State::Stopped);
    }

    /**
     * @brief Request thread to stop
     * @note Interrupts all registered cancellable objects to unblock waiting operations
     */
    void requestStop() {
        // Attempt Running -> Stopping. If not Running, leave unchanged.
        State expected = State::Running;
        if (mState.compare_exchange_strong(expected, State::Stopping,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
            // Successfully transitioned to Stopping - interrupt all cancellables
            interruptCancellables();
        }
    }

    /**
     * @brief Check if stop was requested
     * @retval true Stop was requested
     * @retval false Thread should continue running
     */
    bool shouldStop() const {
        return mState.load(std::memory_order_acquire) == State::Stopping;
    }

    /**
     * @brief Get thread ID
     * @return Thread handle
     */
    osThreadId_t getId() const {
        return mHandle.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if thread is running
     * @retval true Thread is running
     */
    bool isRun() const {
        return getState() == OS::__Thread::State::Running;
    }

#if (configUSE_OS2_THREAD_FLAGS == 1)
    uint32_t flagsSet(uint32_t flags)
    {
        return osThreadFlagsSet(mHandle, flags);
    }

    uint32_t flagsClear(uint32_t flags)
    {
        if (xTaskGetCurrentTaskHandle() != mHandle) {
            return (uint32_t)osError;
        }
        return osThreadFlagsClear(flags);
    }

    uint32_t flagsGet()
    {
        if (xTaskGetCurrentTaskHandle() != mHandle) {
            return (uint32_t)osError;
        }
        return osThreadFlagsGet();
    }

    bool flagsIsSet(uint32_t flags)
    {
        if (xTaskGetCurrentTaskHandle() != mHandle) {
            return false;
        }
        return (osThreadFlagsGet() & flags) != 0;
    }

    uint32_t flagsWait(uint32_t flags, uint32_t options = osFlagsWaitAny,
            uint32_t timeout = osWaitForever)
    {
        if (xTaskGetCurrentTaskHandle() != mHandle) {
            return (uint32_t) osError;
        }

        return osThreadFlagsWait(flags, options, pdMS_TO_TICKS(timeout));
    }

    bool flagsWaitAnyNoClear(uint32_t flags, uint32_t timeout = osWaitForever)
    {
        int32_t rflags = (int32_t) flagsWait(flags,
                osFlagsWaitAny | osFlagsNoClear, timeout);
        if (rflags <= 0) {
            return false;
        }
        return (flags & rflags) != 0;
    }

    bool flagsWaitAllNoClear(uint32_t flags, uint32_t timeout = osWaitForever)
    {
        int32_t rflags = (int32_t) flagsWait(flags,
                osFlagsWaitAll | osFlagsNoClear, timeout);
        if (rflags <= 0) {
            return false;
        }
        return (flags & rflags) != flags;
    }
#endif

private:
    // Thread storage
    StaticTask_t mControlBlock {};
    uint8_t mBuffer[N] {};
    ThreadFunc_t mThreadFunc { nullptr };
    T *mpCallerInstance { nullptr };

    /**
     * @brief Delete the underlying task if this object still holds its handle
     * @note Must run on a DIFFERENT task: deleting another task unlinks its
     *       TCB from every scheduler list synchronously, whereas a self-delete
     *       is deferred to the idle task via xTasksWaitingTermination -- which
     *       would leave the scheduler pointing into this object after the
     *       owner frees it (the use-after-free this class must prevent).
     * @retval true Task deleted (or no task to delete)
     * @retval false Task could NOT be deleted safely -- the caller must keep
     *         this object out of Idle so it cannot be re-initialized or freed
     *         while the scheduler may still reference the control block
     */
    bool terminateTask() {
        osThreadId_t h = mHandle.exchange(nullptr, std::memory_order_acq_rel);
        if (h == nullptr) {
            return true;
        }

        // Runtime-checked (not just asserted): under NDEBUG a self-terminate
        // would silently degrade to the deferred self-delete, and a failed
        // terminate (osErrorResource = the task already died elsewhere and
        // may sit on xTasksWaitingTermination) would let the caller recycle
        // a control block the scheduler still links.
        bool deleted = (h != xTaskGetCurrentTaskHandle());
        if (deleted) {
            deleted = (osThreadTerminate(h) == osOK);
        }
        if (!deleted) {
            configASSERT(false);
            mHandle.store(h, std::memory_order_release);
        }
        return deleted;
    }

    /**
     * @brief Cleanup after failed initialization
     * @note Requests stop, tries to join gracefully, always terminates the
     *       task, and returns thread to Idle state for re-initialization
     */
    void cleanupFailedInit() {
        // Atomically transition Starting/Failed -> Stopping using compare_exchange loop
        State expected = mState.load(std::memory_order_acquire);
        while (expected == State::Starting || expected == State::Failed) {
            if (mState.compare_exchange_strong(expected, State::Stopping,
                                               std::memory_order_release,
                                               std::memory_order_acquire)) {
                break;
            }
            // If CAS failed, expected is updated with current value - retry
        }

        // Give the thread a short window to finish on its own, then delete
        // the task either way: a thread that returned is parked suspended
        // (see rtosThreadEntry) and still needs the synchronous terminate to
        // unlink its TCB before this object's memory can be freed or the
        // control block reused by a re-init.
        join(1000);
        if (!terminateTask()) {
            // Task not deleted: stay out of Idle (see deinit()).
            return;
        }

        // Clear stored instance
        mpCallerInstance = nullptr;
        mThreadFunc = nullptr;
        mInitialized.store(false, std::memory_order_release);
        mAutoInit.store(false, std::memory_order_release);

        // Finally go back to Idle
        mState.store(State::Idle, std::memory_order_release);
    }

    /**
     * @brief RTOS thread entry point
     * @param argument Pointer to Thread2 instance
     * @note Sets handle, auto-initializes if requested, executes user function,
     *       ensures setInitFailed() if user forgot, and marks stopped on exit
     */
    static void rtosThreadEntry(void *argument) {
        Thread *pInstance = static_cast<Thread*>(argument);

        // Store thread handle atomically (so other threads can see it)
        // Note: init() also stores the handle returned by osThreadNew()
        // In FreeRTOS, xTaskGetCurrentTaskHandle() == handle from osThreadNew()
        // This double-store is harmless and ensures handle is set even if
        // osThreadNew() returns before thread starts executing
        pInstance->mHandle.store(xTaskGetCurrentTaskHandle(), std::memory_order_release);

        // If auto-init mode (timeout=0 in init()), automatically mark as initialized
        // User doesn't need to call setInitialized() but can still call it (idempotent)
        if (pInstance->mAutoInit.load(std::memory_order_acquire)) {
            pInstance->setInitialized();
        }

        // Run user thread function
        // If NOT auto-init mode, user MUST call setInitialized() or setInitFailed()
        (pInstance->mpCallerInstance->*pInstance->mThreadFunc)();

        // If user forgot to call setInitialized/setInitFailed (only in non-auto mode),
        // mark as failed
        State cur = pInstance->mState.load(std::memory_order_acquire);
        if (cur == State::Starting) {
            pInstance->setInitFailed();
        }

        // Mark stopped and notify joiners
        pInstance->markStopped();

        // Park instead of exiting. The TCB and stack are members of this
        // Thread object, which may itself live in heap memory (e.g. inside an
        // AppInstance). osThreadExit() would self-delete the task, which
        // FreeRTOS defers to the idle task via xTasksWaitingTermination -- so
        // if the owner is freed after join() but before the idle task runs,
        // the scheduler is left holding list items inside freed memory
        // (field HardFaults in xTaskResumeAll). A suspended task, by
        // contrast, stays validly linked until deinit()'s osThreadTerminate()
        // deletes it from another task, which unlinks the TCB synchronously.
        // The loop guards against a stray resume: returning from a FreeRTOS
        // task function is fatal.
        for (;;) {
            vTaskSuspend(nullptr);
        }
    }
};


/**
 * @brief RAII guard for automatic cancellable registration/unregistration
 * @verbatim
 *   // Example usage:
 *   class MyClass {
 *   private:
 *       Thread2<MyClass, 2048> mThread;
 *       Queue myQueue;
 *
 *       void threadFunc() {
 *           // Register queue with thread for automatic interruption
 *           CancellableGuard guard(&mThread, &myQueue);  // Auto-registers
 *
 *           while (!shouldStop()) {
 *               myQueue.receive();  // Will be interrupted on requestStop()
 *           }
 *           // Auto-unregisters when guard goes out of scope
 *       }
 *   };
 * @endverbatim
 */
class CancellableGuard {
public:
    /**
     * @brief Register cancellable object with thread
     * @param thread Thread to register with
     * @param cancellable Object to register
     */
    CancellableGuard(__Thread* thread, ICancellable* cancellable)
        : mThread(thread), mCancellable(cancellable), mRegistered(false) {
        if (mThread != nullptr && mCancellable != nullptr) {
            mRegistered = mThread->registerCancellable(mCancellable);
        }
    }

    /**
     * @brief Unregister cancellable object
     */
    ~CancellableGuard() {
        if (mRegistered && mThread != nullptr && mCancellable != nullptr) {
            mThread->unregisterCancellable(mCancellable);
        }
    }

    // Non-copyable and non-movable
    CancellableGuard(const CancellableGuard&) = delete;
    CancellableGuard& operator=(const CancellableGuard&) = delete;
    CancellableGuard(CancellableGuard&&) = delete;
    CancellableGuard& operator=(CancellableGuard&&) = delete;

    /**
     * @brief Check if registration was successful
     * @retval true Object was successfully registered
     * @retval false Registration failed
     */
    bool isRegistered() const {
        return mRegistered;
    }

private:
    __Thread* mThread;
    ICancellable* mCancellable;
    bool mRegistered;
};

/**
 * @brief RAII helper for thread initialization
 *
 * Automatically calls setInitFailed() if success() is not called
 */
class ThreadInitGuard {
public:
    ThreadInitGuard(__Thread& thread) : mThread(thread), mSuccess(false) {}

    ~ThreadInitGuard() {
        if (!mSuccess) {
            mThread.setInitFailed();
        }
    }

    void success() {
        mSuccess = true;
        mThread.setInitialized();
    }

private:
    __Thread& mThread;
    bool mSuccess;
};

}  /* namespace OS */

#endif /* __OS_HPP */
