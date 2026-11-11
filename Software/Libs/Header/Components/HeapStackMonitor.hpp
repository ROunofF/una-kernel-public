/**
 ******************************************************************************
 * @file    HeapStackMonitor.hpp
 * @brief   A utility to monitor system and RTOS heap usage and task stack usage.
 ******************************************************************************
 */

#ifndef __HEAP_STACK_MONITOR_HPP
#define __HEAP_STACK_MONITOR_HPP

#include <cstdint>
#include <cstdbool>
#include <cassert>

#include "FreeRTOS.h"
#include "timers.h"
#include "task.h"

#include "Crash/CrashDump.h"

/**
 * @class HeapStackMonitor
 * @brief Monitors heap and stack usage in the system and FreeRTOS.
 */
class HeapStackMonitor {
public:

    /**
     * @brief Get the size of the system heap.
     * @return The total size of the system heap in bytes.
     */
    static size_t GetSysHeapSize();

    /**
     * @brief Get the free size of the system heap.
     * @return The free size of the system heap in bytes.
     */
    static size_t GetSysHeapFree();

    /**
     * @brief Get the minimum system heap watermark.
     * @return The minimum ever free system heap size in bytes.
     */
    static size_t GetSysHeapWaterMark();

    /**
     * @brief Get the size of the FreeRTOS heap.
     * @return The total size of the FreeRTOS heap in bytes.
     */
    static size_t GetOsHeapSize();

    /**
     * @brief Get the free size of the FreeRTOS heap.
     * @return The free size of the FreeRTOS heap in bytes.
     */
    static size_t GetOsHeapFree();

    /**
     * @brief Get the minimum FreeRTOS heap watermark.
     * @return The minimum ever free FreeRTOS heap size in bytes.
     */
    static size_t GetOsHeapWaterMark();

    /**
     * @brief Get information about tasks.
     * @param pInfo: Pointer to an array of TaskStatus_t structures.
     * @param size: Size of the array.
     * @return The number of tasks retrieved.
     */
    static size_t GetTasksInfo(TaskStatus_t *pInfo, size_t size);

    /**
     * @brief Start monitoring with a specific check period.
     * @param checkPeriod: The period in ticks for checks.
     */
    static void Start(uint32_t checkPeriod);

    /**
     * @brief Stop the monitoring process.
     */
    static void Stop();

    /**
     * @brief Get the system heap usage warning threshold.
     * @return Threshold in bytes.
     */
    static uint32_t GetSysHeapThreshold();

    /**
     * @brief Set the system heap usage warning threshold.
     * @param th: Threshold in bytes.
     */
    static void SetSysHeapThreshold(uint32_t th);

    /**
     * @brief Get the FreeRTOS heap usage warning threshold.
     * @return Threshold in bytes.
     */
    static uint32_t GetOsHeapThreshold();

    /**
     * @brief Set the FreeRTOS heap usage warning threshold.
     * @param th: Threshold in bytes.
     */
    static void SetOsHeapThreshold(uint32_t th);

    /**
     * @brief Get the task stack usage warning threshold.
     * @return Threshold in bytes.
     */
    static uint32_t GetTaskStackThreshold();

    /**
     * @brief Set the task stack usage warning threshold.
     * @param th: Threshold in bytes.
     */
    static void SetTaskStackThreshold(uint32_t th);

    /**
     * @brief Last periodic heap sample, for embedding in the crash dump.
     *
     * Points at static storage updated in place on every check; wire it to
     * CrashDumpConfig::heapSample. Fields are up to one check period stale
     * and all-zero until the first check runs.
     */
    static const volatile CrashHeapSample* GetHeapSample();

private:

    /// FreeRTOS heap usage threshold in bytes.
    static size_t mOsHeapTreshold;

    /// System heap usage threshold in bytes.
    static size_t mSysHeapTreshold;

    /// Task stack usage threshold in bytes.
    static size_t mStackTreshold;

    /// Timer handle for periodic monitoring.
    static TimerHandle_t mTimHandle;

    /// Control block for static timer allocation.
    static StaticTimer_t mTimControlBlock;

    /// Maximum number of tasks monitored.
    static const uint32_t skTasksMax = 64;

    /// Array to store task information.
    static TaskStatus_t mTaskInfoArray[skTasksMax];

    /// Evaluate the periodic heap INFO line every this many checks (it is
    /// then emitted only if something meaningful changed -- see Check()).
    static const uint32_t skInfoEveryNChecks = 10;

    /// Minimum free-heap movement (bytes) that counts as a meaningful change
    /// for the periodic INFO line, so background jitter on an idle watch does
    /// not defeat the change-gating and spam the log.
    static const uint32_t skInfoFreeDeltaBytes = 1024;

    /// Number of completed checks (drives the INFO-line cadence).
    static uint32_t mCheckCount;

    /// True minimum ever sampled free system heap (min over check samples).
    static size_t mSysHeapMinEver;

    /// Snapshot of the last emitted periodic INFO line, used to gate re-emits
    /// on meaningful change (see Check()). mLastInfoValid is false until the
    /// first line is emitted.
    static bool mLastInfoValid;
    static size_t mLastInfoFreeHeap;
    static size_t mLastInfoMinEver;
    static uint32_t mLastInfoNotifCount;
    static uint32_t mLastInfoScanCount;
    static uint32_t mLastInfoAppBytes;
    static uint32_t mLastInfoAppBlocks;

    /// Deepest MSP use seen so far, so the residue dump below fires only when
    /// the stack sets a new record instead of on every sample.
    static size_t mIsrStackPeak;

    /// Last periodic heap sample, read by the crash dump in fault context.
    static volatile CrashHeapSample mHeapSample;

    /**
     * @brief Log what the deepest MSP excursion left on the stack.
     *
     * The watermark says how deep the main stack went; it does not say what
     * went there. This prints the raw words and interprets none of them: the
     * watch has no symbol table and cannot tell a return address from a
     * rodata word. That is done offline, against the ELF.
     *
     * @param used Bytes of MSP in use at the new peak.
     */
    static void DumpIsrStackResidue(size_t used);

    /**
     * @brief Internal function to check heap and stack usage.
     * @param xTimer Timer handle invoking this function.
     */
    static void Check(TimerHandle_t xTimer);
};

#endif /* __HEAP_STACK_MONITOR_HPP */
