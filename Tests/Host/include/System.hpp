/**
 * Host-test stub for the system driver.
 *
 * Only the millisecond tick, and it does not run: it sits where a test puts
 * it, so a timing result does not depend on the speed of the host.
 */
#ifndef HOST_STUB_SYSTEM_HPP
#define HOST_STUB_SYSTEM_HPP

#include <atomic>
#include <cstdint>

namespace Driver
{

class System
{

public:

    static uint32_t GetTimeMs()
    {
        return sTimeMs.load(std::memory_order_acquire);
    }

    /// Atomic: a worker thread reads it while the test writes it.
    static void SetTimeMs(uint32_t ms)
    {
        sTimeMs.store(ms, std::memory_order_release);
    }

    static void AdvanceMs(uint32_t ms)
    {
        sTimeMs.fetch_add(ms, std::memory_order_acq_rel);
    }

private:

    inline static std::atomic<uint32_t> sTimeMs { 0 };

};

} // namespace Driver

#endif // HOST_STUB_SYSTEM_HPP
