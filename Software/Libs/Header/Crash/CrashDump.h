/**
 ******************************************************************************
 * @file    CrashDump.h
 * @brief   Portable crash dump capture for Cortex-M.
 *
 * Captures a crash capsule (fault registers, r4-r11, both stacks, RTOS task
 * table, pre-reset log) into a dedicated no-init linker section (`.crash_dump`)
 * and persists it via a sink on the next boot. Depends only on CMSIS-Core; all
 * host services (identity, memory ranges, reset cause, time, log, RTOS
 * introspection, persistence) are injected through @ref CrashDumpConfig.
 *
 * Supported sources:
 *  - HardFault (exception-based capture)
 *  - IWDG early-wakeup interrupt (WATCHDOG) (exception-based capture)
 *  - Assert (forced exception via UDF; reason marked as ASSERT)
 *  - FreeRTOS stack-overflow hook
 *  - Synthesized on boot after a watchdog reset that left no capsule
 *
 * Design notes:
 *  - Fixed-size binary structures (stable for offline tools).
 *  - Validity is indicated by a commit marker written last.
 *  - No dynamic memory and no libc dependency in the fault path.
 ******************************************************************************
 */

#ifndef CRASH_DUMP_H
#define CRASH_DUMP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of bytes to snapshot from a stack pointer upwards.
 *
 * Captured once per stack (MSP and PSP). Sized to give the offline CFI
 * unwinder enough reach to walk several frames.
 */
#define CRASH_STACK_SNAPSHOT_BYTES  (2048u)

/**
 * @brief Maximum number of RTOS tasks recorded in the dump.
 *
 * The task table is best-effort: if the system has more tasks than this, the
 * excess is dropped (taskCount reflects how many were stored).
 */
#define CRASH_MAX_TASKS             (32u)

/**
 * @brief Size of the log region embedded in the persisted dump file.
 *
 * The log bytes come from the configured log provider; this is only the
 * fixed-size region reserved for them in the on-disk dump (hdr+pld+log+crc).
 */
#define CRASH_LOG_BYTES             (64u * 1024u)

/**
 * @brief Cortex-M exception stack frame (basic frame).
 *
 * These 8 words are automatically pushed by the CPU on exception entry.
 * The naked exception wrappers pass a pointer to this frame as stackedRegs[0..7].
 *
 * Layout:
 *   r0,r1,r2,r3,r12,lr,pc,xpsr
 *
 * @note If an extended (FP) frame is present, additional words are stacked beyond
 *       the basic frame. This structure describes only the basic 8-word frame.
 */
typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
} ExceptionStackFrame;

/**
 * @brief Callee-saved registers (r4..r11) at fault entry.
 *
 * Not part of the CPU exception frame; the naked exception wrappers push them
 * before any C runs and pass a pointer here. Together with the exception frame
 * and the stack snapshot they give the offline tool a full core register file
 * for DWARF/CFI unwinding.
 */
typedef struct {
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
} CalleeSavedRegs;

/**
 * @brief Per-task record captured for the RTOS task table.
 *
 * Best-effort snapshot of every task known to the scheduler at fault time.
 * For a watchdog/hang the offending task is usually not the active one, so the
 * whole table is recorded.
 */
typedef struct {
    char     name[16];     /**< Task name (truncated to fit). */
    uint32_t handle;       /**< TCB pointer (task identity). */
    uint32_t topOfStack;   /**< Saved stack pointer (TCB->pxTopOfStack). */
    uint32_t stackBase;    /**< Stack region start (TCB->pxStack). */
    uint32_t state;        /**< eTaskState (Running/Ready/Blocked/Suspended/Deleted). */
    uint32_t priority;     /**< Current priority. */
    uint32_t highWater;    /**< Stack high-water mark in words (free remaining). */
} CrashTaskInfo;

/**
 * @brief Heap-attribution sample embedded in the crash payload (format v5+).
 *
 * Copied field-by-field from the integrator's last periodic heap sample
 * (@ref CrashDumpConfig::heapSample); deliberately NOT measured in fault
 * context, where the heap itself may be the corrupt thing and mallinfo would
 * take the (possibly held) malloc lock. Fields are therefore up to one
 * monitor period stale, and adjacent fields may mix two consecutive periods.
 * All-zero when no sample was ever published (crash before the monitor
 * started, no integrator sample, or a boot-synthesized watchdog capsule).
 */
typedef struct {
    uint32_t freeHeap;      /**< Free system heap: dlmalloc fordblks + sbrk headroom. */
    uint32_t minEverFree;   /**< Minimum ever sampled freeHeap (true watermark). */
    uint32_t sbrkHeadroom;  /**< Bytes sbrk has never handed to newlib (0 = fragmentation-bound). */
    uint32_t fordblks;      /**< dlmalloc free bytes inside the arena. */
    uint32_t ordblks;       /**< dlmalloc free chunk count. */
    uint32_t notifCount;    /**< GUI phone-notification list size. */
    uint32_t scanCount;     /**< GUI accessory-scan-result list size. */
    uint32_t appAllocBytes; /**< Outstanding App::MemAllocator bytes (all apps). */
} CrashHeapSample;

/**
 * @brief Crash capsule header.
 *
 * The header provides basic metadata and memory ranges needed for offline analysis.
 *
 * @note Field @ref CrashHeader::committed is written last. If a reset happens mid-write,
 *       it will not match the commit marker and the dump must be ignored.
 */
typedef struct {
    uint32_t magic;             /**< "CRSH" = 0x48535243 */
    uint32_t version;           /**< On-disk format version (currently 5) */
    uint32_t dumpSize;          /**< Total on-disk dump size (hdr+pld+log+crc) for sanity checks */
    uint32_t committed;         /**< Commit marker when valid (written last) */
} CrashHeader;

/**
 * @brief Crash capsule payload.
 *
 * Contains CPU context captured at exception entry and a best-effort stack snapshot.
 *
 * The stack snapshot is stored as raw bytes; offline tools typically interpret it
 * as 32-bit words (little-endian) and scan for return addresses.
 */
typedef struct {
    uint32_t logSize;           /**< sizeof(CrashDump.log) */
    uint32_t timestamp;         /**< Best-effort uptime timestamp in ms */

    char     projectName[64];   /**< Project name */
    char     buildVersion[64];  /**< Kernel version */
    char     bootVersion[32];   /**< Bootloader version */
    char     libcVersion[32];   /**< Stdlib version */

    uint32_t flashStart;        /**< Flash start address */
    uint32_t flashEnd;          /**< Flash end address (exclusive) */
    uint32_t ramStart;          /**< RAM start address */
    uint32_t ramEnd;            /**< RAM end address (exclusive) */

    /**
     * @brief Logical cause of the dump as a string.
     *
     * Expected values: "HARDFAULT", "ASSERT", "WATCHDOG".
     */
    char crash_reason[16];

    /** CPU-saved exception frame (basic frame: R0..xPSR). */
    ExceptionStackFrame esf;

    /** EXC_RETURN value (LR on exception entry); indicates MSP/PSP, return mode, etc. */
    uint32_t excReturn;

    /** Main Stack Pointer at capture time. */
    uint32_t msp;

    /** Process Stack Pointer at capture time (tasks typically run on PSP). */
    uint32_t psp;

    /** CONTROL register (privilege / SP selection in Thread mode). */
    uint32_t control;

    /** IPSR (active exception number). */
    uint32_t ipsr;

    /** System Control Block fault status registers. */
    uint32_t cfsr;   /**< Configurable Fault Status Register */
    uint32_t hfsr;   /**< HardFault Status Register */
    uint32_t dfsr;   /**< Debug Fault Status Register */
    uint32_t afsr;   /**< Auxiliary Fault Status Register */
    uint32_t mmfar;  /**< MemManage Fault Address Register (valid only if MMARVALID set) */
    uint32_t bfar;   /**< BusFault Address Register (valid only if BFARVALID set) */

    uint32_t shcsr;  /**< SCB->SHCSR (fault enable/active/pending) */
    uint32_t ccr;    /**< SCB->CCR   (UNALIGN_TRP, DIV_0_TRP, etc.) */
    uint32_t icsr;   /**< SCB->ICSR  (active vector / pending) */
    uint32_t vtor;   /**< SCB->VTOR  (vector table base) */
    uint32_t cpacr;  /**< SCB->CPACR (FPU access control) */

    /** Set to 1 if dump was triggered by ASSERT. */
    uint32_t assertFlag;

    /** Source file path for ASSERT (best-effort; may be truncated). */
    char     assertFile[256];

    /** Source line for ASSERT. */
    uint32_t assertLine;

    /** Current task name (best-effort). Empty if unknown / pre-RTOS. */
    char taskName[32];

    /** Set to 1 if @ref CrashPayload::callee holds valid r4..r11. */
    uint32_t calleeValid;

    /** Callee-saved registers (r4..r11) at fault entry. */
    CalleeSavedRegs callee;

    /**
     * @brief Raw RCC->CSR reset-flag word latched at boot (before clear).
     *
     * Decoded offline into power-on / pin / brown-out / software / IWDG / WWDG.
     * Captures the reason for the current boot even when no capsule was made.
     */
    uint32_t resetFlags;

    /** Which stack held the faulting context: 0 = MSP, 1 = PSP. */
    uint32_t activeStack;

    /** Valid bytes in @ref CrashPayload::mspStack (snapshot from @ref msp). */
    uint32_t mspBytes;

    /** Valid bytes in @ref CrashPayload::pspStack (snapshot from @ref psp). */
    uint32_t pspBytes;

    /** Number of valid entries in @ref CrashPayload::tasks. */
    uint32_t taskCount;

    /** RTOS task table (best-effort; up to @ref CRASH_MAX_TASKS entries). */
    CrashTaskInfo tasks[CRASH_MAX_TASKS];

    /** Stack snapshot copied from @ref msp upwards (mspBytes valid). */
    uint8_t  mspStack[CRASH_STACK_SNAPSHOT_BYTES];

    /** Stack snapshot copied from @ref psp upwards (pspBytes valid). */
    uint8_t  pspStack[CRASH_STACK_SNAPSHOT_BYTES];

    /**
     * Last periodic heap sample (appended in v5; keep appending after this so
     * the offline parser's fixed offsets stay valid). See @ref CrashHeapSample
     * for staleness semantics; all-zero when unavailable.
     */
    CrashHeapSample heap;
} CrashPayload;

/**
 * @brief In-RAM crash capsule (header + payload only).
 *
 * Kept in the no-init .crash_dump section so it survives a warm reset. The log
 * region and trailing CRC are not stored here: the log is sourced from an
 * external provider and both are assembled only when persisting to a file.
 *
 * On disk the dump keeps the historical layout: hdr + pld + log[CRASH_LOG_BYTES]
 * + crc32. Keep @ref CrashPayload byte-stable across versions; if it changes,
 * bump @ref CrashHeader::version and adjust offline parsers accordingly.
 */
#pragma pack(push, 4)
typedef struct {
    CrashHeader  hdr;   /**< Header metadata */
    CrashPayload pld;   /**< Captured payload */
} CrashDump;
#pragma pack(pop)

/**
 * @brief Linearized view of the log to embed into the dump (up to two spans).
 *
 * The provider exposes the log as one or two contiguous spans in oldest->newest
 * order (a ring split). CrashDump copies them into the dump file and pads the
 * rest of the log region with zeros. CrashDump stays agnostic of the source.
 */
typedef struct {
    const uint8_t* part1;   /**< Oldest span (may be NULL if empty). */
    uint32_t       len1;    /**< Length of part1 in bytes. */
    const uint8_t* part2;   /**< Newer span after a ring wrap (NULL/0 if none). */
    uint32_t       len2;    /**< Length of part2 in bytes. */
} CrashLogView;

/**
 * @brief Callback that fills a @ref CrashLogView with the current log spans.
 * @param[out] view Spans to embed (oldest->newest). Set lengths to 0 if empty.
 * @param[in]  ctx  Opaque context (CrashDumpConfig::logCtx).
 */
typedef void (*CrashLogProvider_t)(CrashLogView* view, void* ctx);

/**
 * @brief RTOS introspection port (optional; any field may be NULL).
 *
 * The module never calls an RTOS directly; the integrator wires these.
 */
typedef struct {
    /**
     * Fill up to @p maxCount task records; return the number written.
     * Runs in fault context: must not lock or block. NULL -> no task table.
     */
    uint32_t (*taskSnapshot)(CrashTaskInfo* out, uint32_t maxCount, void* ctx);

    /**
     * Report the running task's stack as [low, highExcl) and its name.
     * Return false if unavailable (then the PSP snapshot is clamped to RAM only
     * and the task name is left empty). NULL -> same as returning false.
     */
    bool (*currentStack)(uint32_t* low, uint32_t* highExcl, const char** name, void* ctx);

    void* ctx;   /**< Opaque context passed to both callbacks. */
} CrashDumpRtos;

/**
 * @brief Streaming sink for the assembled dump (hdr+pld+log+crc), in write order.
 *
 * The integrator owns the destination (file name/path, medium). @ref open is
 * called once, then @ref write zero or more times, then @ref close. A NULL
 * sink (or NULL open/write) disables persistence.
 */
typedef struct {
    bool (*open)(void* ctx);                                  /**< Begin a dump; false aborts. */
    bool (*write)(const uint8_t* data, uint32_t len, void* ctx); /**< Append bytes; false aborts. */
    void (*close)(bool ok, void* ctx);                        /**< Finish; ok = all writes succeeded. */
    void* ctx;
} CrashDumpSink;

/**
 * @brief All host-provided dependencies of the module, set once via CrashDump_init.
 *
 * Keeps the module dependency-free: identity, memory ranges, reset cause, time
 * source, log/RTOS/persistence ports are all injected here. Set before any
 * fault can occur (ranges/rtos/clock are read in the fault path) and before
 * @ref CrashDump_check (reset cause + sink are read there).
 */
typedef struct {
    /** Identity stamped into the dump (pointers must outlive the module). */
    const char* projectName;
    const char* buildVersion;
    const char* bootVersion;
    const char* libcVersion;

    /** Flash range (for the offline parser) and RAM range (stack-snapshot clamp). */
    uint32_t flashStart;
    uint32_t flashEnd;
    uint32_t ramStart;
    uint32_t ramEnd;

    /** Raw reset-flag word for this boot (decoded offline). */
    uint32_t resetFlags;
    /** True if this boot was caused by a watchdog reset (drives no-capsule synth). */
    bool     watchdogReset;

    /** Optional uptime/timestamp source (ms), read at capture time. NULL -> 0. */
    uint32_t (*nowMs)(void);

    /** Optional pre-reset log provider. */
    CrashLogProvider_t logProvider;
    void*              logCtx;

    /** Optional RTOS introspection and persistence ports. */
    CrashDumpRtos rtos;
    CrashDumpSink sink;

    /**
     * @brief Optional notification after a crash dump is persisted (or fails).
     *
     * Called once per saved/synthesized capsule with the crash reason and
     * whether the sink write succeeded. The integrator owns the file name, so
     * this is where a one-line "crash saved to <file>" log belongs.
     */
    void (*onSaved)(const char* reason, bool ok, void* ctx);
    void* onSavedCtx;

    /**
     * @brief Optional crash-safe console writer for an at-the-moment breadcrumb.
     *
     * Invoked once from the capture path, after the capsule is committed and
     * just before reset, with a short pre-formatted line
     * ("CRASH: <reason> PC=.. LR=.. CFSR=.. task=..\r\n"). MUST be safe to call
     * from a fault handler: bounded and non-blocking (e.g. a register-level UART
     * poll). Lets a test/dev build see what crashed on the console when the
     * capsule is not persisted to a file. No line is emitted for ASSERT, which
     * reports its richer file/line/expr line at its own call site.
     */
    void (*consoleWrite)(const char* data, uint32_t len, void* ctx);
    void* consoleWriteCtx;

    /**
     * @brief Optional pointer to the integrator's live heap sample.
     *
     * Copied word-by-word into @ref CrashPayload::heap when a capsule is
     * captured (fault context: no locks are taken, the sample is never
     * computed there). Must point at statically allocated memory that the
     * integrator updates in place. NULL leaves the payload sample all-zero.
     */
    const volatile CrashHeapSample* heapSample;
} CrashDumpConfig;

void CrashDump_handlerHardFault(uint32_t* stackedRegs, uint32_t excReturn, uint32_t* calleeRegs);
void CrashDump_handlerWatchDog(uint32_t* stackedRegs, uint32_t excReturn, uint32_t* calleeRegs);
// Not marked noreturn on purpose: that lets GCC drop everything after the call
// in __assert_func, so the call's return slot falls into the next function and
// the offline unwinder loses the assert backtrace. The caller keeps a trailing
// loop so the call is not its last instruction.
void CrashDump_handlerAssert(const char* file, uint32_t line,
                             const char* func, const char* expr);
void CrashDump_handlerStackOverflow(const char* taskName);

/**
 * @brief Wire all host dependencies. Call once early in boot.
 *
 * Stores a copy of @p cfg. Must run before a fault (ranges/rtos/clock are used
 * in capture) and before @ref CrashDump_check (reset cause + sink are used there).
 */
void CrashDump_init(const CrashDumpConfig* cfg);

void CrashDump_check(void);

#ifdef __cplusplus
}
#endif

#endif  // CRASH_DUMP_H
