/**
 * Host-test stub for UsTimer.hpp.
 *
 * The kernel microsecond timer sits on an STM32 hardware counter and cannot
 * compile on the host. Drivers need it only for a vendor SensorAPI delay
 * callback, and a host test never waits on real time, so the delay is a no-op.
 */
#ifndef HOST_STUB_US_TIMER_HPP
#define HOST_STUB_US_TIMER_HPP

#include <cstdint>

namespace Driver
{

class UsTimer
{
public:
    static void Init() {}
    static void DelayUs(uint32_t /*us*/) {}
};

} // namespace Driver

#endif // HOST_STUB_US_TIMER_HPP
