/**
 * Host-test stub for OS.hpp.
 *
 * The kernel OS abstraction (Software/Libs/Header/Drivers/OS.hpp) wraps
 * FreeRTOS and cannot compile on the host. This stands in for it on std::
 * primitives - a real worker thread, a real blocking queue - because the
 * components under test are built from them.
 *
 * Differences from the kernel version:
 *
 *   - Delay() does not sleep; it records what was asked for.
 *   - Queue timeouts are honoured in real time; Mutex::lock() and
 *     Thread::join() ignore theirs and wait. Anything measured in seconds
 *     belongs in a test of the decision, not of the waiting.
 */
#ifndef HOST_STUB_OS_HPP
#define HOST_STUB_OS_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "System.hpp"

/// Host code only passes this through; it has to exist and mean forever.
constexpr uint32_t osWaitForever = 0xFFFFFFFFu;

/// The CMSIS-RTOS values, so kernel code that sets a priority compiles here.
/// Host threads are not scheduled by them.
enum osPriority_t {
    osPriorityLow         =  8,
    osPriorityBelowNormal = 16,
    osPriorityNormal      = 24,
    osPriorityAboveNormal = 32,
    osPriorityHigh        = 40,
};

namespace OS
{

/// Milliseconds asked for since a test last cleared it, so code that paces
/// itself can be covered. Tests that read it set it to zero first.
inline uint32_t gDelayedMs = 0;

inline void Delay(uint32_t ms) { gDelayedMs += ms; }

/// Yields asked for since a test last cleared it. Atomic because host tests
/// yield from several threads; tests that read it set it to zero first.
inline std::atomic<uint32_t> gYields {0};

/// Runs inside Yield() when set, standing in for whatever another thread
/// would do while this one gave up the CPU - so a test can make "the receiver
/// drained during the yield" happen deterministically. Tests that set it
/// clear it again.
inline std::function<void()> gOnYield;

inline void Yield()
{
    gYields.fetch_add(1, std::memory_order_relaxed);
    if (gOnYield) {
        gOnYield();
    }
    std::this_thread::yield();
}

/// The system stub's clock, so code reading either one sees the same time.
inline uint32_t GetTimeMs() { return Driver::System::GetTimeMs(); }

/**
 * @brief   Something a blocked wait can be woken out of.
 *
 * A stopping thread interrupts everything registered with it, so a parked
 * worker leaves promptly instead of after its timeout.
 */
class ICancellable
{
public:
    virtual ~ICancellable() = default;
    virtual void interrupt() = 0;
};

/**
 * @brief   Host only: lets a test wait for a worker to finish what was queued
 *          to it. Every push is counted, and whenever a pop is about to wait
 *          on an empty queue it records the count: everything pushed until
 *          then has been taken. Shared by every queue, so it can only tell
 *          when a worker is done while there is one worker.
 */
struct QueueProbe
{
    static uint64_t pushes()    { return sPushes.load(std::memory_order_acquire); }
    static uint64_t drainedAt() { return sDrainedAt.load(std::memory_order_acquire); }

    inline static std::atomic<uint64_t> sPushes    { 0 };
    inline static std::atomic<uint64_t> sDrainedAt { 0 };
};

/**
 * @brief   Blocking queue, bounded at N.
 *
 * Cancellation pushes a default-built item, as the kernel version does; the
 * worker recognises it by its own rules.
 */
template<typename T, size_t N>
class Queue : public ICancellable
{

public:

    Queue() = default;

    ~Queue() override { deinit(); }

    bool init(const char* name)
    {
        (void) name;

        std::lock_guard<std::mutex> lock(mMutex);
        mItems.clear();
        mAlive = true;

        return true;
    }

    void deinit()
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mAlive = false;
        }

        // Anything parked has to leave before the object does.
        mNotEmpty.notify_all();
        mNotFull.notify_all();
    }

    bool push(const T& item, uint32_t timeout = osWaitForever)
    {
        std::unique_lock<std::mutex> lock(mMutex);

        if (!mAlive) {
            return false;
        }

        if (!waitFor(lock, mNotFull, timeout,
                     [this] { return !mAlive || (mItems.size() < N); })) {
            return false;
        }

        if (!mAlive) {
            return false;
        }

        mItems.push_back(item);
        QueueProbe::sPushes.fetch_add(1, std::memory_order_acq_rel);
        lock.unlock();
        mNotEmpty.notify_one();

        return true;
    }

    bool pop(T& item, uint32_t timeout = osWaitForever)
    {
        std::unique_lock<std::mutex> lock(mMutex);

        // Under the lock every push to this queue takes, so the count read
        // here includes all of them.
        if (mAlive && mItems.empty() && (timeout != 0)) {
            QueueProbe::sDrainedAt.store(
                    QueueProbe::sPushes.load(std::memory_order_acquire),
                    std::memory_order_release);
        }

        if (!waitFor(lock, mNotEmpty, timeout,
                     [this] { return !mAlive || !mItems.empty(); })) {
            return false;
        }

        if (mItems.empty()) {
            return false;
        }

        item = mItems.front();
        mItems.pop_front();
        lock.unlock();
        mNotFull.notify_one();

        return true;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mItems.clear();
    }

    size_t   capacity() const { return N; }
    bool     empty() const    { return size() == 0; }

    uint32_t size() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return static_cast<uint32_t>(mItems.size());
    }

    uint32_t available() const
    {
        return static_cast<uint32_t>(N) - size();
    }

    void interrupt() override
    {
        T dummy {};
        (void) push(dummy, 0);
    }

private:

    /// A zero timeout tests the predicate and returns, never parks: the only
    /// form callable from an interrupt on the target.
    template<typename Pred>
    bool waitFor(std::unique_lock<std::mutex>& lock,
                 std::condition_variable& cv, uint32_t timeout, Pred pred)
    {
        if (pred()) {
            return true;
        }

        if (timeout == 0) {
            return false;
        }

        if (timeout == osWaitForever) {
            cv.wait(lock, pred);
            return true;
        }

        return cv.wait_for(lock, std::chrono::milliseconds(timeout), pred);
    }

    mutable std::mutex      mMutex;
    std::condition_variable mNotEmpty;
    std::condition_variable mNotFull;
    std::deque<T>           mItems;
    bool                    mAlive = false;

};

/**
 * @brief   Mutex with an explicit lifecycle, as the kernel one has.
 * @note    A mutex that was never init()ed fails to lock rather than blocking,
 *          as the kernel version does with a null handle; callers rely on it.
 */
class Mutex
{

public:

    Mutex() = default;
    ~Mutex() { deinit(); }

    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;

    /// Whether creating a mutex fails. Set through MutexInitFailure rather
    /// than directly, so it cannot be left on for the next test.
    static inline bool sInitFails = false;

    bool init(const char* name, bool recursive = false)
    {
        (void) name;
        (void) recursive;

        if (sInitFails) {
            return false;
        }

        mAlive = true;

        return true;
    }

    void deinit() { mAlive = false; }

    bool lock(uint32_t timeout = osWaitForever)
    {
        (void) timeout;

        if (!mAlive) {
            return false;
        }

        mMutex.lock();

        return true;
    }

    void release()
    {
        if (mAlive) {
            mMutex.unlock();
        }
    }

private:

    std::mutex mMutex;
    bool       mAlive = false;

};

/// While one of these is alive, no mutex can be created.
class MutexInitFailure
{

public:

    MutexInitFailure() { Mutex::sInitFails = true; }

    ~MutexInitFailure() { Mutex::sInitFails = false; }

    MutexInitFailure(const MutexInitFailure&)            = delete;
    MutexInitFailure& operator=(const MutexInitFailure&) = delete;

};

/**
 * @brief   Scope guard over a Mutex.
 * @note    Ignores whether the lock succeeded, as the kernel one does, so
 *          callers check that their mutex exists first.
 */
class MutexCS
{

public:

    explicit MutexCS(Mutex& mutex) : mMutex(mutex) { mMutex.lock(osWaitForever); }

    ~MutexCS() { mMutex.release(); }

    MutexCS(const MutexCS&)            = delete;
    MutexCS& operator=(const MutexCS&) = delete;

private:

    Mutex& mMutex;

};

/**
 * @brief   Worker thread, with the stop-and-join lifecycle the kernel one has.
 * @note    The stack size N is ignored here; kept so component code compiles.
 */
template<typename T, size_t N>
class Thread
{

public:

    typedef void (T::*ThreadFunc_t)();

    Thread() = default;

    ~Thread() { deinit(); }

    Thread(const Thread&)            = delete;
    Thread& operator=(const Thread&) = delete;

    bool init(const char* name, T* instance, ThreadFunc_t func,
              osPriority_t prio = osPriorityNormal, uint32_t initTimeoutMs = 0)
    {
        (void) name;
        (void) prio;
        (void) initTimeoutMs;

        if ((instance == nullptr) || (func == nullptr) || mThread.joinable()) {
            return false;
        }

        mStop.store(false);
        mThread = std::thread([instance, func] { (instance->*func)(); });

        return true;
    }

    void deinit()
    {
        requestStop();

        if (mThread.joinable()) {
            mThread.join();
        }
    }

    /// Ask the worker to finish, and wake whatever it is parked on so it can.
    void requestStop()
    {
        mStop.store(true, std::memory_order_release);

        for (ICancellable* c : mCancellables) {
            if (c != nullptr) {
                c->interrupt();
            }
        }
    }

    bool shouldStop() const { return mStop.load(std::memory_order_acquire); }

    bool join(uint32_t timeoutMs = osWaitForever)
    {
        (void) timeoutMs;

        if (mThread.joinable()) {
            mThread.join();
        }

        return true;
    }

    bool registerCancellable(ICancellable* cancellable)
    {
        for (ICancellable*& slot : mCancellables) {
            if (slot == nullptr) {
                slot = cancellable;
                return true;
            }
        }

        return false;
    }

private:

    static constexpr size_t skMaxCancellables = 4;

    std::thread       mThread;
    std::atomic<bool> mStop { false };
    ICancellable*     mCancellables[skMaxCancellables] = {};

};

} // namespace OS

#endif // HOST_STUB_OS_HPP
