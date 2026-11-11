/**
 ******************************************************************************
 * @file    CrashDump.cpp
 * @brief   Crash dump capture for Cortex-M.
 *
 * Captures a "crash capsule" into a dedicated no-init linker section
 * (.crash_dump) and persists it on next boot. The module depends only on
 * CMSIS-Core (SCB / NVIC / __disable_irq); identity, memory ranges, reset
 * cause, time, logging, RTOS introspection and persistence are injected by the
 * integrator via @ref CrashDumpConfig (see CrashDump_init).
 *
 * Supported sources:
 *  - HardFault (with exception stack frame)
 *  - IWDG early wakeup (WATCHDOG) (with exception stack frame)
 *  - Assert (forced exception via UDF; reason marked as ASSERT)
 *  - FreeRTOS stack-overflow hook
 ******************************************************************************
 */

#include "Crash/CrashDump.h"
#include "stm32u5xx.h"     // CMSIS device header: SCB, NVIC_SystemReset, __disable_irq

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Optional diagnostic logging hook.
 *
 * No-op by default so the module stays self-contained. Define CRASHDUMP_LOG
 * (e.g. in a build flag) to route the plain-string messages to a logger.
 */
#ifndef CRASHDUMP_LOG
#define CRASHDUMP_LOG(msg)  ((void)(msg))
#endif

#define CRASH_COMMIT_WORD  (0xA5A55A5Au)   /**< Marker indicating a valid committed dump. */
#define CRASH_MAGIC_WORD   (0x48535243u)   /**< "CRSH" */

/// Crash dump storage. Placed into dedicated section to survive until persisted.
static __attribute__((section(".crash_dump"))) __attribute__((used))
CrashDump sCrashDump;

/**
 * @brief Bounded save-retry counter (no-init, survives reset).
 *
 * Counted before each save attempt so a capsule that faults mid-write cannot
 * loop forever. Zeroed whenever a capsule is created (commit / synthesize) and
 * on any boot without a committed capsule, so cold-boot garbage never sticks.
 */
static __attribute__((section(".crash_dump"))) __attribute__((used))
uint32_t sSaveAttempts;

#define CRASH_MAX_SAVE_ATTEMPTS  (3u)

/// Host-provided dependencies (set once by CrashDump_init).
static CrashDumpConfig sCfg;

/**
 * @brief Minimal assert metadata passed to the exception path.
 *
 * Stored as pointers (file string lives in flash .rodata). The dump payload stores
 * a copied file path and line (assertFile/assertLine), so the parser does not need ELF.
 */
typedef struct {
    volatile uint32_t pending;  /**< When true, next HardFault is treated as ASSERT. */
    const char*       file;     /**< Source file where assert triggered. */
    uint32_t          line;     /**< Line number where assert triggered. */
} AssertConfig;

static AssertConfig sAssertConfig;

/**
 * @brief Fetch the current log spans from the configured provider.
 *
 * Returns an empty view when no provider is set.
 */
static void CrashDump_getLogView(CrashLogView* view)
{
    view->part1 = nullptr;
    view->len1  = 0;
    view->part2 = nullptr;
    view->len2  = 0;

    if (sCfg.logProvider) {
        sCfg.logProvider(view, sCfg.logCtx);
    }
}

/**
 * @brief Check whether an address belongs to the configured RAM range.
 */
static inline int CrashDump_isInRAM(uint32_t addr)
{
    return (addr >= sCfg.ramStart && addr < sCfg.ramEnd);
}

static bool CrashDump_isExceptionFrameReadable(const uint32_t* stackedRegs)
{
    const uint32_t addr = (uint32_t)stackedRegs;
    const uint32_t end  = addr + 7u * sizeof(uint32_t);

    return ((addr & 0x3u) == 0u) &&
           (end >= addr) &&
           CrashDump_isInRAM(addr) &&
           CrashDump_isInRAM(end);
}

/**
 * @brief Minimal memset without libc dependency (safe inside fault handlers).
 */
static void CrashDump_memset(uint8_t* dst, uint8_t value, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        dst[i] = value;
    }
}

/**
 * @brief Minimal memcpy without libc dependency (safe inside fault handlers).
 */
static void CrashDump_memcpy(uint8_t* dst, const uint8_t* src, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

/**
 * @brief Minimal strlen without libc dependency.
 */
static size_t CrashDump_strlen(const char* src)
{
    size_t s = 0;
    while (*src != '\0') {
        ++s;
        ++src;
    }
    return s;
}

/**
 * @brief Copy C-string into fixed buffer (best-effort).
 *
 * If src is longer than destination, copies the tail so the most relevant part
 * (often filename) remains visible.
 */
static void CrashDump_strcpy(char* dst, const char* src, size_t n)
{
    if (n == 0 || dst == nullptr) {
        return;
    }

    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }

    size_t len = CrashDump_strlen(src);

    if (len >= n) {
        src += len - (n - 1);
        len = n - 1;
    }

    CrashDump_memcpy((uint8_t*)dst, (const uint8_t*)src, len);

    dst[len] = '\0';
}

/**
 * @brief Streaming zlib-compatible CRC32 (poly 0xEDB88320), seed-chained.
 *
 * Call with crc = 0 for the first chunk and feed the result back for the next.
 * Matches Python's zlib.crc32 so the offline parser can verify the dump.
 */
static uint32_t CrashDump_crc32(uint32_t crc, const uint8_t* buf, uint32_t len)
{
    crc = crc ^ 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            uint32_t mask = (uint32_t)(0u - (crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void CrashDump_setCrashReason(const char* reason)
{
    if (reason) {
        CrashDump_strcpy(sCrashDump.pld.crash_reason, reason, sizeof(sCrashDump.pld.crash_reason));
    } else {
        sCrashDump.pld.crash_reason[0] = '\0';
    }
}

/**
 * @brief Clear crash dump storage (invalidate previous dump).
 */
static void CrashDump_init_storage(void)
{
    CrashDump_memset((uint8_t*)&sCrashDump, 0x00, sizeof(sCrashDump));
}

/**
 * @brief Check whether a committed dump is present in .crash_dump section.
 */
static bool CrashDump_isCommited(void)
{
    return (sCrashDump.hdr.magic     == CRASH_MAGIC_WORD &&
            sCrashDump.hdr.committed == CRASH_COMMIT_WORD);
}

// Append up to maxSrc chars of s (stopping at NUL) into dst[n..cap); return new n.
static uint32_t CrashDump_putStr(char* dst, uint32_t cap, uint32_t n,
                                 const char* s, uint32_t maxSrc)
{
    for (uint32_t i = 0; i < maxSrc && s[i] != '\0' && n < cap; ++i) {
        dst[n++] = s[i];
    }
    return n;
}

// Append "0x" + 8 hex nibbles of v into dst[n..cap); return new n.
static uint32_t CrashDump_putHex32(char* dst, uint32_t cap, uint32_t n, uint32_t v)
{
    static const char kHex[] = "0123456789ABCDEF";
    if (n < cap) dst[n++] = '0';
    if (n < cap) dst[n++] = 'x';
    for (uint32_t i = 0; i < 8u && n < cap; ++i) {
        dst[n++] = kHex[(v >> (28u - i * 4u)) & 0xFu];
    }
    return n;
}

// Append the decimal digits of v into dst[n..cap); return new n.
static uint32_t CrashDump_putDec(char* dst, uint32_t cap, uint32_t n, uint32_t v)
{
    char     tmp[10];
    uint32_t t = 0;
    do {
        tmp[t++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u && t < sizeof(tmp));
    while (t > 0u && n < cap) {
        dst[n++] = tmp[--t];
    }
    return n;
}

static bool CrashDump_reasonIs(const char* reason)
{
    const char* r = sCrashDump.pld.crash_reason;
    uint32_t i = 0;
    while (r[i] == reason[i]) {
        if (reason[i] == '\0') {
            return true;
        }
        ++i;
    }
    return false;
}

/**
 * @brief Emit the assert's file/line/expr line to the integrator's console port.
 *
 * Runs in thread context (before the forced fault), so the strings can be
 * dereferenced safely. Goes through the same consoleWrite port as the breadcrumb,
 * so the integrator persists + echoes it the same way. Any of func/expr may be
 * null. The generic breadcrumb is skipped for ASSERT (see CrashDump_emitConsole).
 */
static void CrashDump_emitAssert(const char* file, uint32_t line,
                                 const char* func, const char* expr)
{
    if (sCfg.consoleWrite == nullptr) {
        return;
    }

    // Static, not automatic, and the reason is stack rather than style.
    // This frame is 304 bytes - the single largest thing that ever runs on the
    // MSP stack, which every interrupt shares and which was unguarded when this
    // was written. Moving the
    // buffer to .bss takes that off the peak the reservation has to cover.
    //
    // Safe because this path is not re-entrant: the only caller is
    // CrashDump_handlerAssert, which masks interrupts first and ends in a
    // forced fault. It shares nothing with line[] in CrashDump_emitConsole;
    // the two are kept separate so a later change to the call order cannot
    // make them alias.
    static char    buff[256];
    const uint32_t cap = sizeof(buff);
    uint32_t       n   = 0;

    n = CrashDump_putStr(buff, cap, n, "assert: ", cap);
    n = CrashDump_putStr(buff, cap, n, (file != nullptr) ? file : "?", cap);
    n = CrashDump_putStr(buff, cap, n, ":", cap);
    n = CrashDump_putDec(buff, cap, n, line);
    if (expr != nullptr) {
        n = CrashDump_putStr(buff, cap, n, ": ", cap);
        n = CrashDump_putStr(buff, cap, n, expr, cap);
    }
    if (func != nullptr) {
        n = CrashDump_putStr(buff, cap, n, " (", cap);
        n = CrashDump_putStr(buff, cap, n, func, cap);
        n = CrashDump_putStr(buff, cap, n, ")", cap);
    }
    n = CrashDump_putStr(buff, cap, n, "\r\n", cap);

    sCfg.consoleWrite(buff, n, sCfg.consoleWriteCtx);
}

/**
 * @brief Emit a one-line crash breadcrumb to the integrator's console port.
 *
 * Best-effort and fault-context safe: fixed-size buffer, no snprintf, all fields
 * are fixed arrays / uint32 (no wild pointers). Skipped for ASSERT, whose richer
 * file/line/expr line is emitted from CrashDump_handlerAssert instead.
 */
static void CrashDump_emitConsole(void)
{
    if (sCfg.consoleWrite == nullptr || CrashDump_reasonIs("ASSERT")) {
        return;
    }

    // Static for the same reason as buff in CrashDump_emitAssert: this
    // runs from the fault handler, on the shared MSP stack, and 128 bytes of
    // frame there is worth more than 128 bytes of .bss. Only caller is
    // CrashDump_commit, reached with interrupts already masked.
    static char    line[128];
    const uint32_t cap = sizeof(line);
    uint32_t       n   = 0;

    n = CrashDump_putStr(line, cap, n, "\r\nCRASH: ", cap);
    n = CrashDump_putStr(line, cap, n, sCrashDump.pld.crash_reason,
                         sizeof(sCrashDump.pld.crash_reason));
    n = CrashDump_putStr(line, cap, n, " PC=", cap);
    n = CrashDump_putHex32(line, cap, n, sCrashDump.pld.esf.pc);
    n = CrashDump_putStr(line, cap, n, " LR=", cap);
    n = CrashDump_putHex32(line, cap, n, sCrashDump.pld.esf.lr);
    n = CrashDump_putStr(line, cap, n, " CFSR=", cap);
    n = CrashDump_putHex32(line, cap, n, sCrashDump.pld.cfsr);
    n = CrashDump_putStr(line, cap, n, " task=", cap);
    n = CrashDump_putStr(line, cap, n, sCrashDump.pld.taskName,
                         sizeof(sCrashDump.pld.taskName));
    n = CrashDump_putStr(line, cap, n, "\r\n", cap);

    sCfg.consoleWrite(line, n, sCfg.consoleWriteCtx);
}

static void CrashDump_commit(void)
{
    sCrashDump.pld.timestamp = (sCfg.nowMs != nullptr) ? sCfg.nowMs() : 0u;

    sSaveAttempts = 0;   // fresh capsule -> fresh save-retry budget

    sCrashDump.hdr.magic     = CRASH_MAGIC_WORD;
    sCrashDump.hdr.committed = CRASH_COMMIT_WORD;

    // Best-effort console breadcrumb: capsule is now valid, reset is imminent.
    CrashDump_emitConsole();
}

static bool CrashDump_isClean(void)
{
    const uint8_t* buff = (uint8_t*)&sCrashDump;
    for (uint32_t i = 0; i < (sizeof(sCrashDump.hdr) + sizeof(sCrashDump.pld)); ++i) {
        if (buff[i] != 0x00) {
            return false;
        }
    }

    return true;
}

// Force a reset after capturing the crash capsule
static void CrashDump_reset(void)
{
    NVIC_SystemReset();
    while (1) {}
}

/**
 * @brief Capture CPU context: EXC_RETURN, MSP/PSP/CONTROL/IPSR, SCB fault regs.
 */
static void CrashDump_captureCpuContext(uint32_t excReturn, uint32_t* msp, uint32_t* psp)
{
    sCrashDump.pld.excReturn = excReturn;

    uint32_t control, ipsr;
    __asm volatile("mrs %0, msp"     : "=r"(*msp));
    __asm volatile("mrs %0, psp"     : "=r"(*psp));
    __asm volatile("mrs %0, control" : "=r"(control));
    __asm volatile("mrs %0, ipsr"    : "=r"(ipsr));

    sCrashDump.pld.msp     = *msp;
    sCrashDump.pld.psp     = *psp;
    sCrashDump.pld.control = control;
    sCrashDump.pld.ipsr    = ipsr;

    sCrashDump.pld.cfsr  = SCB->CFSR;
    sCrashDump.pld.hfsr  = SCB->HFSR;
    sCrashDump.pld.dfsr  = SCB->DFSR;
    sCrashDump.pld.afsr  = SCB->AFSR;
    sCrashDump.pld.mmfar = SCB->MMFAR;
    sCrashDump.pld.bfar  = SCB->BFAR;

    sCrashDump.pld.shcsr = SCB->SHCSR;
    sCrashDump.pld.ccr   = SCB->CCR;
    sCrashDump.pld.icsr  = SCB->ICSR;
    sCrashDump.pld.vtor  = SCB->VTOR;

#if defined(SCB_CPACR_CP10_Msk) || defined(SCB_CPACR_CP11_Msk)
    sCrashDump.pld.cpacr = SCB->CPACR;
#else
    sCrashDump.pld.cpacr = 0;
#endif
}

/**
 * @brief Store callee-saved registers (r4..r11) captured by the naked wrapper.
 *
 * @param[in] calleeRegs Pointer to 8 words r4..r11, or NULL if not captured.
 */
static void CrashDump_captureCallee(const uint32_t* calleeRegs)
{
    if (calleeRegs == nullptr) {
        sCrashDump.pld.calleeValid = 0;
        return;
    }

    sCrashDump.pld.callee.r4  = calleeRegs[0];
    sCrashDump.pld.callee.r5  = calleeRegs[1];
    sCrashDump.pld.callee.r6  = calleeRegs[2];
    sCrashDump.pld.callee.r7  = calleeRegs[3];
    sCrashDump.pld.callee.r8  = calleeRegs[4];
    sCrashDump.pld.callee.r9  = calleeRegs[5];
    sCrashDump.pld.callee.r10 = calleeRegs[6];
    sCrashDump.pld.callee.r11 = calleeRegs[7];
    sCrashDump.pld.calleeValid = 1;
}

/**
 * @brief Best-effort stack snapshot from @p sp upwards into @p dst.
 *
 * Only reads from RAM. When @p haveBounds is set (PSP/current task), the snapshot
 * end is clamped to [low, high); otherwise only the RAM end limits it.
 *
 * @return Number of valid bytes copied (0 if @p sp is not usable).
 */
static uint32_t CrashDump_captureStackFrom(uint32_t sp, uint8_t* dst,
                                           bool haveBounds, uint32_t low, uint32_t high)
{
    if (!CrashDump_isInRAM(sp)) {
        return 0;
    }

    uint32_t end = sp + CRASH_STACK_SNAPSHOT_BYTES;

    if (haveBounds &&
        (low < high) &&
        (sp >= low) &&
        (sp < high) &&
        CrashDump_isInRAM(low) &&
        CrashDump_isInRAM(high - 1u)) {
        if (end > high) {
            end = high;   // clamp to task stack end
        }
    }

    if (end > sCfg.ramEnd) {
        end = sCfg.ramEnd;
    }

    end &= ~0x3u;   // align to 4 bytes

    if (end <= sp) {
        return 0;
    }

    uint32_t n = end - sp;
    if (n > CRASH_STACK_SNAPSHOT_BYTES) {
        n = CRASH_STACK_SNAPSHOT_BYTES;
    }

    CrashDump_memcpy(dst, (const uint8_t*)sp, n);
    return n;
}

/**
 * @brief Snapshot both MSP and PSP stacks and record which one faulted.
 *
 * @param[in] excReturn  EXC_RETURN value (bit2 selects the faulting stack).
 * @param[in] msp        Captured MSP.
 * @param[in] psp        Captured PSP.
 * @param[in] haveBounds Whether [low, high) holds the running task's stack bounds.
 */
static void CrashDump_captureStacks(uint32_t excReturn, uint32_t msp, uint32_t psp,
                                    bool haveBounds, uint32_t low, uint32_t high)
{
    sCrashDump.pld.mspBytes    = CrashDump_captureStackFrom(msp, sCrashDump.pld.mspStack,
                                                            false, 0, 0);
    sCrashDump.pld.pspBytes    = CrashDump_captureStackFrom(psp, sCrashDump.pld.pspStack,
                                                            haveBounds, low, high);
    sCrashDump.pld.activeStack = ((excReturn & 0x4u) != 0) ? 1u : 0u;
}

/**
 * @brief Capture the RTOS task table via the integrator's provider (best-effort).
 *
 * For a watchdog/hang the offending task is usually not the active one, so the
 * whole scheduler task set is recorded. Empty if no provider is configured.
 */
static void CrashDump_captureTasks(void)
{
    sCrashDump.pld.taskCount = 0;

    if (sCfg.rtos.taskSnapshot == nullptr) {
        return;
    }

    uint32_t n = sCfg.rtos.taskSnapshot(sCrashDump.pld.tasks, CRASH_MAX_TASKS, sCfg.rtos.ctx);
    if (n > CRASH_MAX_TASKS) {
        n = CRASH_MAX_TASKS;
    }
    sCrashDump.pld.taskCount = n;
}

/**
 * @brief Copy the integrator's last periodic heap sample into the payload.
 *
 * Deliberately does not query the allocator here: in fault context the heap
 * itself may be the corrupt thing, and mallinfo would take the (possibly
 * held) malloc lock. The word-sized copies of a concurrently updated sample
 * are best-effort: fields may mix two adjacent monitor periods.
 */
static void CrashDump_captureHeap(void)
{
    const volatile CrashHeapSample* s = sCfg.heapSample;
    if (s == nullptr) {
        return;   // payload was zeroed by the caller: sample reads all-zero
    }

    sCrashDump.pld.heap.freeHeap      = s->freeHeap;
    sCrashDump.pld.heap.minEverFree   = s->minEverFree;
    sCrashDump.pld.heap.sbrkHeadroom  = s->sbrkHeadroom;
    sCrashDump.pld.heap.fordblks      = s->fordblks;
    sCrashDump.pld.heap.ordblks       = s->ordblks;
    sCrashDump.pld.heap.notifCount    = s->notifCount;
    sCrashDump.pld.heap.scanCount     = s->scanCount;
    sCrashDump.pld.heap.appAllocBytes = s->appAllocBytes;
}

/**
 * @brief Resolve the running task's name and stack bounds via the RTOS port.
 *
 * Sets the dump's task name and returns whether bounds were obtained.
 */
static bool CrashDump_captureCurrentTask(uint32_t* low, uint32_t* high)
{
    *low  = 0;
    *high = 0;

    if (sCfg.rtos.currentStack == nullptr) {
        return false;
    }

    const char* name = nullptr;
    bool ok = sCfg.rtos.currentStack(low, high, &name, sCfg.rtos.ctx);
    if (name) {
        CrashDump_strcpy(sCrashDump.pld.taskName, name, sizeof(sCrashDump.pld.taskName));
    }
    return ok;
}

/**
 * @brief Common crash capture routine for exception-based sources.
 *
 * @note This function ends with NVIC_SystemReset().
 */
static void CrashDump_handlerCommon(uint32_t* stackedRegs, uint32_t excReturn, uint32_t* calleeRegs)
{
    // Exception stack frame: stackedRegs points to r0..xPSR
    sCrashDump.pld.esf.r0   = stackedRegs[0];
    sCrashDump.pld.esf.r1   = stackedRegs[1];
    sCrashDump.pld.esf.r2   = stackedRegs[2];
    sCrashDump.pld.esf.r3   = stackedRegs[3];
    sCrashDump.pld.esf.r12  = stackedRegs[4];
    sCrashDump.pld.esf.lr   = stackedRegs[5];
    sCrashDump.pld.esf.pc   = stackedRegs[6];
    sCrashDump.pld.esf.xpsr = stackedRegs[7];

    CrashDump_captureCallee(calleeRegs);

    uint32_t msp = 0, psp = 0;
    CrashDump_captureCpuContext(excReturn, &msp, &psp);

    uint32_t low = 0, high = 0;
    const bool haveBounds = CrashDump_captureCurrentTask(&low, &high);

    CrashDump_captureStacks(excReturn, msp, psp, haveBounds, low, high);
    CrashDump_captureTasks();
    CrashDump_captureHeap();

    // Commit marker: indicates the dump is valid
    CrashDump_commit();

    // Force a reset after capturing the crash capsule
    CrashDump_reset();
}

/**
 * @brief Capture path when the exception stack frame is unreadable.
 *
 * The CPU frame (r0..xPSR) cannot be trusted, but the callee-saved registers
 * captured by the naked wrapper and both stack snapshots are still recorded.
 */
static void CrashDump_handlerInvalidStackPointer(uint32_t excReturn, uint32_t* calleeRegs)
{
    CrashDump_captureCallee(calleeRegs);

    uint32_t msp = 0, psp = 0;
    CrashDump_captureCpuContext(excReturn, &msp, &psp);

    uint32_t low = 0, high = 0;
    const bool haveBounds = CrashDump_captureCurrentTask(&low, &high);

    CrashDump_captureStacks(excReturn, msp, psp, haveBounds, low, high);
    CrashDump_captureTasks();
    CrashDump_captureHeap();

    CrashDump_commit();
    CrashDump_reset();
}

static void CrashDump_finalize(void)
{
    CRASHDUMP_LOG("completing the crash dump");

    sCrashDump.hdr.version  = 5;   // v5: CrashHeapSample appended to the payload
    // On-disk layout: hdr + pld + log[CRASH_LOG_BYTES] + crc32.
    sCrashDump.hdr.dumpSize = sizeof(sCrashDump) + CRASH_LOG_BYTES + sizeof(uint32_t);

    CrashDump_strcpy(sCrashDump.pld.projectName,  sCfg.projectName,  sizeof(sCrashDump.pld.projectName));
    CrashDump_strcpy(sCrashDump.pld.buildVersion, sCfg.buildVersion, sizeof(sCrashDump.pld.buildVersion));
    CrashDump_strcpy(sCrashDump.pld.bootVersion,  sCfg.bootVersion,  sizeof(sCrashDump.pld.bootVersion));
    CrashDump_strcpy(sCrashDump.pld.libcVersion,  sCfg.libcVersion,  sizeof(sCrashDump.pld.libcVersion));

    sCrashDump.pld.flashStart = sCfg.flashStart;
    sCrashDump.pld.flashEnd   = sCfg.flashEnd;
    sCrashDump.pld.ramStart   = sCfg.ramStart;
    sCrashDump.pld.ramEnd     = sCfg.ramEnd;

    // Raw reset-flag word for this boot; decoded offline. For a synthesized
    // capsule it carries the watchdog reset the fault path never recorded.
    sCrashDump.pld.resetFlags = sCfg.resetFlags;
}

/**
 * @brief Write a chunk to the sink and fold it into the running CRC32.
 */
static bool CrashDump_sinkWrite(uint32_t* crc, const uint8_t* data, uint32_t len)
{
    if (len == 0) {
        return true;
    }

    *crc = CrashDump_crc32(*crc, data, len);
    return sCfg.sink.write(data, len, sCfg.sink.ctx);
}

static bool CrashDump_save(void)
{
    CRASHDUMP_LOG("save the crash dump image");

    // Source the pre-reset log spans and record the exact valid length so the
    // parser reads precisely the captured bytes.
    CrashLogView view;
    CrashDump_getLogView(&view);

    uint32_t logValid = view.len1 + view.len2;
    if (logValid > CRASH_LOG_BYTES) {
        logValid = CRASH_LOG_BYTES;
    }
    sCrashDump.pld.logSize = logValid;

    bool ok = false;

    if (sCfg.sink.open != nullptr && sCfg.sink.write != nullptr &&
        sCfg.sink.open(sCfg.sink.ctx)) {
        uint32_t crc = 0;
        ok = true;

        // hdr + pld (contiguous in the in-RAM capsule)
        ok = ok && CrashDump_sinkWrite(&crc, (const uint8_t*)&sCrashDump, sizeof(sCrashDump));

        // log region: valid spans (clamped) then zero padding up to CRASH_LOG_BYTES
        uint32_t len1 = view.len1;
        uint32_t len2 = view.len2;
        if (len1 > CRASH_LOG_BYTES) {
            len1 = CRASH_LOG_BYTES;
        }
        if (len1 + len2 > CRASH_LOG_BYTES) {
            len2 = CRASH_LOG_BYTES - len1;
        }
        ok = ok && CrashDump_sinkWrite(&crc, view.part1, len1);
        ok = ok && CrashDump_sinkWrite(&crc, view.part2, len2);

        static const uint8_t kZeros[256] = { 0 };
        uint32_t pad = CRASH_LOG_BYTES - (len1 + len2);
        while (ok && pad > 0) {
            uint32_t chunk = (pad < sizeof(kZeros)) ? pad : sizeof(kZeros);
            ok = CrashDump_sinkWrite(&crc, kZeros, chunk);
            pad -= chunk;
        }

        // trailing CRC32 (over hdr + pld + log), raw (not folded into itself)
        ok = ok && sCfg.sink.write((const uint8_t*)&crc, sizeof(crc), sCfg.sink.ctx);

        if (sCfg.sink.close) {
            sCfg.sink.close(ok, sCfg.sink.ctx);
        }
    } else {
        CRASHDUMP_LOG("crash dump sink unavailable");
    }

    if (!ok) {
        CRASHDUMP_LOG("failed to write the crash dump");
    }

    // Notify the integrator (which owns the file name) that a crash was persisted.
    if (sCfg.onSaved) {
        sCfg.onSaved(sCrashDump.pld.crash_reason, ok, sCfg.onSavedCtx);
    }

    return ok;
}

/**
 * @brief Capture crash dump for watchdog early interrupt.
 */
void CrashDump_handlerWatchDog(uint32_t* stackedRegs, uint32_t excReturn, uint32_t* calleeRegs)
{
    __disable_irq();

    CrashDump_memset((uint8_t*)&sCrashDump, 0x00, sizeof(sCrashDump.hdr) + sizeof(sCrashDump.pld));

    // The exception stack frame must live in RAM (normal MSP/PSP stacks).
    if (!CrashDump_isExceptionFrameReadable(stackedRegs)) {
        CrashDump_setCrashReason("WD_BAD_ESF");                          // ESF - Exception stack frame
        CrashDump_handlerInvalidStackPointer(excReturn, calleeRegs);    // This function never returns.
    }

    CrashDump_setCrashReason("WATCHDOG");
    CrashDump_handlerCommon(stackedRegs, excReturn, calleeRegs);
}

/**
 * @brief Capture crash dump for HardFault.
 *
 * If an assert is pending, the dump is marked as ASSERT and contains assertFile/assertLine.
 */
void CrashDump_handlerHardFault(uint32_t* stackedRegs, uint32_t excReturn, uint32_t* calleeRegs)
{
    __disable_irq();

    CrashDump_memset((uint8_t*)&sCrashDump, 0x00, sizeof(sCrashDump.hdr) + sizeof(sCrashDump.pld));

    // Invalid/corrupted exception stack pointer.
    // Capture minimal fault context and force system reset.
    if (!CrashDump_isExceptionFrameReadable(stackedRegs)) {
        CrashDump_setCrashReason("HF_BAD_ESF");                          // ESF - Exception stack frame
        CrashDump_handlerInvalidStackPointer(excReturn, calleeRegs);    // This function never returns.
    }

    if (sAssertConfig.pending) {
        sAssertConfig.pending = false;

        CrashDump_setCrashReason("ASSERT");

        sCrashDump.pld.assertFlag = 1;
        sCrashDump.pld.assertLine = sAssertConfig.line;
        CrashDump_strcpy(sCrashDump.pld.assertFile, sAssertConfig.file, sizeof(sCrashDump.pld.assertFile));
    } else {
        CrashDump_setCrashReason("HARDFAULT");
    }

    CrashDump_handlerCommon(stackedRegs, excReturn, calleeRegs);    // This function never returns.
}

/**
 * @brief Mark an assert, log it, and enter the exception path to capture a dump.
 *
 * Runs in thread context: latches the ASSERT attribution, emits the human-readable
 * file/line/expr line via the console port (so func/expr - which live only here -
 * are not lost), then forces a fault (UDF) to obtain an exception stack frame and
 * reuse the HardFault capture path. @p func and @p expr may be null. Does not
 * return.
 */
void CrashDump_handlerAssert(const char* file, uint32_t line,
                             const char* func, const char* expr)
{
    // Stop the world first - we are committed to crashing. With all interrupts
    // masked no nested assert can occur, and emitAssert needs no interrupts
    // (bounded register/memory ops only), so its output stays uncontended by any
    // ISR. Latch attribution before logging, so a fault inside emitAssert is still
    // recorded as ASSERT (consumed by CrashDump_handlerHardFault).
    __disable_irq();

    sAssertConfig.file    = file;
    sAssertConfig.line    = line;
    sAssertConfig.pending = 1;

    CrashDump_emitAssert(file, line, func, expr);

    // Forcefully make an exception
    __asm volatile("udf #0");

    while (1) {}
}

void CrashDump_handlerStackOverflow(const char* taskName)
{
    __disable_irq();

    CrashDump_memset((uint8_t*)&sCrashDump, 0x00, sizeof(sCrashDump.hdr) + sizeof(sCrashDump.pld));
    CrashDump_setCrashReason("STACK_OVERFLOW");

    CrashDump_strcpy(sCrashDump.pld.taskName,
                     taskName,
                     sizeof(sCrashDump.pld.taskName));

    // The offending task is known; the whole task table aids overflow diagnosis.
    CrashDump_captureTasks();
    CrashDump_captureHeap();

    CrashDump_commit();

    // Force a reset after capturing the crash capsule
    NVIC_SystemReset();
    while (1) {}
}

/**
 * @brief Fabricate a minimal capsule for a watchdog reset that left no capsule.
 *
 * If the system hung with interrupts disabled, the IWDG early interrupt never
 * fired and no fault handler ran, so no CPU context exists. The pre-hang log
 * survived in no-init RAM and is still embedded by the normal save path; the
 * exact watchdog (IWDG/WWDG) is decoded offline from resetFlags.
 */
static void CrashDump_synthesizeResetCapsule(void)
{
    CrashDump_memset((uint8_t*)&sCrashDump, 0x00, sizeof(sCrashDump.hdr) + sizeof(sCrashDump.pld));

    CrashDump_setCrashReason("WATCHDOG_RESET");

    sSaveAttempts = 0;   // fresh capsule -> fresh save-retry budget

    sCrashDump.hdr.magic     = CRASH_MAGIC_WORD;
    sCrashDump.hdr.committed = CRASH_COMMIT_WORD;

    // timestamp stays 0: the hang time is unknown. Deliberately no console
    // breadcrumb - this runs at boot, not at the hang, so the line would land
    // after the reboot's own log output: misplaced and misleading. The watchdog
    // reset is already conveyed by the reason, the reset flags and onSaved().
}

void CrashDump_init(const CrashDumpConfig* cfg)
{
    if (cfg) {
        sCfg = *cfg;
    }
}

/**
 * @brief Persist a committed crash dump, then clear the crash area.
 *
 * Intended to be called on boot. A valid (or synthesized watchdog) capsule is
 * written through the sink and cleared only on confirmed success, so a transient
 * sink failure retries on the next boot instead of losing the dump. A bounded
 * attempt counter stops a capsule that faults mid-write from looping forever.
 */
void CrashDump_check(void)
{
    CRASHDUMP_LOG("check for the crash dump image");

    bool haveCapsule = CrashDump_isCommited();

    if (!haveCapsule) {
        if (sCfg.watchdogReset) {
            CRASHDUMP_LOG("watchdog reset with no capsule; synthesizing one");
            CrashDump_synthesizeResetCapsule();   // resets the attempt counter, marks committed
            haveCapsule = true;
        } else {
            // Clean boot (or stale garbage): nothing to persist.
            if (!CrashDump_isClean()) {
                CRASHDUMP_LOG("the crash dump is not clean");
            }
            sSaveAttempts = 0;            // keep the no-init counter sane across cold boots
            CrashDump_init_storage();
            return;
        }
    }

    // A capsule that faults mid-write must not retry forever.
    if (sSaveAttempts >= CRASH_MAX_SAVE_ATTEMPTS) {
        CRASHDUMP_LOG("crash dump exceeded save attempts; dropping");
        sSaveAttempts = 0;
        CrashDump_init_storage();
        return;
    }
    sSaveAttempts++;   // counted before the attempt so a fault mid-write is bounded

    CrashDump_finalize();

    if (CrashDump_save()) {
        sSaveAttempts = 0;
        CrashDump_init_storage();        // clear ONLY on a confirmed successful persist
    }
    // else: keep the capsule + counter -> retry on the next boot (bounded above)
}
