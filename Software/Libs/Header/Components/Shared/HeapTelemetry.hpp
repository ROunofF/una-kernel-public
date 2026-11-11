/**
 ******************************************************************************
 * @file    HeapTelemetry.hpp
 * @brief   Lock-free gauges for the known heap-hungry containers.
 *
 * Owners (the GUI thread for the Model lists, app threads for MemAllocator)
 * publish their sizes with relaxed atomics at the existing insert/erase
 * sites; HeapStackMonitor reads them from its timer task and folds them into
 * the periodic system-heap log line and the crash-dump heap sample, so a
 * field heap OOM is attributable to a specific consumer instead of anonymous.
 *
 * Function-local statics (instead of inline variables) keep the header usable
 * from every dialect the GUI sources are built with, on target and simulator.
 ******************************************************************************
 */

#ifndef __HEAP_TELEMETRY_HPP
#define __HEAP_TELEMETRY_HPP

#include <atomic>
#include <cstdint>

namespace HeapTelemetry
{

/// GUI phone-notification list size (Model::mPhoneNotifications).
inline std::atomic<uint32_t>& PhoneNotificationCount()
{
    static std::atomic<uint32_t> count{0};
    return count;
}

/// GUI accessory-scan-result list size (Model::mAccessoryScanResults).
inline std::atomic<uint32_t>& AccessoryScanResultCount()
{
    static std::atomic<uint32_t> count{0};
    return count;
}

/// Outstanding App::MemAllocator bytes, aggregated over all apps. Best-effort
/// telemetry: each allocator retracts its own contribution wholesale via a
/// private mirror in freeAll(), so the aggregate stays essentially exact
/// across app runs. A force-terminate tearing a lock-free update can bias the
/// aggregate slightly high (never low/wrapped); that bias is bounded and
/// non-accumulating per run but persists until reboot (see MemAllocator.cpp
/// freeAll()).
inline std::atomic<uint32_t>& AppAllocBytes()
{
    static std::atomic<uint32_t> bytes{0};
    return bytes;
}

/// Outstanding App::MemAllocator block count, aggregated over all apps. Same
/// best-effort mirror-retraction contract as AppAllocBytes().
inline std::atomic<uint32_t>& AppAllocBlocks()
{
    static std::atomic<uint32_t> blocks{0};
    return blocks;
}

} // namespace HeapTelemetry

#endif // __HEAP_TELEMETRY_HPP
