/**
 ******************************************************************************
 * @file    crash_glue.cpp
 * @brief   CrashDump integration glue for the UnaWatch kernel.
 *
 * Implements the project-specific CrashDump ports (log provider, FreeRTOS
 * introspection, FatFs persistence, identity/ranges/reset cause) and hands
 * them to the portable module via CrashDump_init. See CrashGlue_setup.
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Crash"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "crash_glue.h"

#include "Crash/CrashDump.h"
#include "crash_task_snapshot.h"

#include "FreeRTOS.h"
#include "task.h"

#include "System.hpp"
#include "File.hpp"
#include "Directory.hpp"
#include "LogUart.hpp"
#include "LogBuffer/LogBuffer.hpp"
#include "Components/Sink/SessionFiles.hpp"
#include "HeapStackMonitor.hpp"
#include "main.h"

#include <ctime>
#include <cstdio>
#include <new>   // std::nothrow

/** @brief Keep at most this many dump files; SessionFiles::prepare prunes the oldest. */
static constexpr uint32_t kCrashDumpKeep = 8;

extern uint8_t __ram1_start__,  __ram1_end__;
extern uint8_t __flash_start__, __flash_end__;
extern void * volatile pxCurrentTCB;

/**
 * @brief Log provider: exposes the LogBuffer ring as 1-2 spans (oldest->newest).
 */
static void CrashGlue_logProvider(CrashLogView *view, void *ctx)
{
    (void)ctx;

    const uint8_t *base = LogBuffer::data();
    uint32_t sz   = LogBuffer::size();
    uint32_t head = LogBuffer::head();
    uint32_t cap  = LogBuffer::capacity();

    if (sz < cap) {
        view->part1 = base;        view->len1 = sz;
        view->part2 = nullptr;     view->len2 = 0;
    } else {
        view->part1 = base + head; view->len1 = cap - head;
        view->part2 = base;        view->len2 = head;
    }
}

static uint32_t CrashGlue_nowMs(void)
{
    return Driver::System::GetTimeMs();
}

/** @brief RTOS port: snapshot all tasks (fault-safe) and map to the dump's record type. */
static uint32_t CrashGlue_taskSnapshot(CrashTaskInfo *out, uint32_t maxCount, void *ctx)
{
    (void)ctx;

    if (pxCurrentTCB == nullptr) {
        return 0;   // scheduler not running yet
    }

    static TaskSnapshot_t snap[CRASH_MAX_TASKS];
    uint32_t cap = (maxCount < CRASH_MAX_TASKS) ? maxCount : CRASH_MAX_TASKS;
    UBaseType_t n = xTaskSnapshotAll(snap, cap);
    if (n > cap) {
        n = cap;
    }

    for (UBaseType_t i = 0; i < n; ++i) {
        CrashTaskInfo *t = &out[i];

        size_t k = 0;
        const char *s = snap[i].pcName;
        if (s) {
            for (; (k + 1u) < sizeof(t->name) && s[k] != '\0'; ++k) {
                t->name[k] = s[k];
            }
        }
        t->name[k]    = '\0';
        t->handle     = (uint32_t)snap[i].pvHandle;
        t->topOfStack = snap[i].ulTopOfStack;
        t->stackBase  = snap[i].ulStackBase;
        t->state      = snap[i].ulState;
        t->priority   = snap[i].ulPriority;
        t->highWater  = snap[i].ulHighWater;
    }
    return (uint32_t)n;
}

/** @brief Whether @p p points into RAM1 (a wild pointer must not be dereferenced). */
static bool CrashGlue_inRam(const void *p)
{
    const uint32_t a = (uint32_t)p;
    return a >= (uint32_t)&__ram1_start__ && a < (uint32_t)&__ram1_end__;
}

/** @brief RTOS port: running task's stack bounds + name (for PSP snapshot clamping). */
static bool CrashGlue_currentStack(uint32_t *low, uint32_t *highExcl, const char **name, void *ctx)
{
    (void)ctx;

    // A smashed scheduler (e.g. a stack overflow) can leave pxCurrentTCB wild;
    // xTaskGetCurrentName/StackBounds only null-check it, so range-check here to
    // avoid re-faulting inside the crash handler. The bounds it returns are still
    // validated by the module before the snapshot is read.
    if (!CrashGlue_inRam((const void *)pxCurrentTCB)) {
        return false;
    }

    if (name) {
        *name = xTaskGetCurrentName();
    }
    return xTaskGetCurrentStackBounds(low, highExcl) == pdTRUE;
}

/** @brief Persistence port: a timestamped file under the crash dump directory. */
static FS::File *sCrashFile;
static char      sCrashPath[320];   /**< Path of the current dump (for the saved-log). */

static bool CrashGlue_sinkOpen(void *ctx)
{
    (void)ctx;

    FS::Directory::Create(CRASH_DUMP_DIR_PATH);

    const char *devId = Driver::System::GetDeviceIdStr();
    const char *ver   = Driver::System::GetBuildVersionStr();

    // Prefix up to and including the '_' before NNNN. The device id is constant,
    // so retention scans every dump for this device across boots.
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "dump_%s_", devId);
    const uint32_t idx =
        Sink::SessionFiles::prepare(CRASH_DUMP_DIR_PATH, prefix, ".bin", kCrashDumpKeep);

    sCrashFile = new (std::nothrow) FS::File();
    if (!sCrashFile) {
        return false;
    }

    // Naming follows the session-file convention plus a build-version suffix:
    //   dump_<id>_<NNNN>_<YYYYMMDDTHHMMSS>_<ver>.bin
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(sCrashPath, sizeof(sCrashPath),
             "%sdump_%s_%04lu_%04d%02d%02dT%02d%02d%02d_%s.bin",
             CRASH_DUMP_DIR_PATH, devId, (unsigned long)idx,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, ver);

    sCrashFile->setPath(sCrashPath);
    if (!sCrashFile->open(true, true)) {
        delete sCrashFile;
        sCrashFile = nullptr;
        return false;
    }
    return true;
}

static bool CrashGlue_sinkWrite(const uint8_t *data, uint32_t len, void *ctx)
{
    (void)ctx;
    if (!sCrashFile) {
        return false;
    }
    size_t bw = 0;
    return sCrashFile->write((const char *)data, len, bw) && (bw == len);
}

static void CrashGlue_sinkClose(bool ok, void *ctx)
{
    (void)ctx;
    (void)ok;
    if (sCrashFile) {
        sCrashFile->close();
        delete sCrashFile;
        sCrashFile = nullptr;
    }
}

/** @brief Logged on boot when a crash from the previous run was persisted (or failed). */
static void CrashGlue_onSaved(const char *reason, bool ok, void *ctx)
{
    (void)ctx;
    if (ok) {
        LOG_WARNING("dump saved: reason=%s file=%s\n", reason, sCrashPath);
    } else {
        LOG_ERROR("dump for reason=%s was not saved\n", reason);
    }
}

/** @brief Crash-safe transport for the at-the-moment crash breadcrumb.
 *
 * Persists into the no-init LogBuffer (so the line survives the reset and lands
 * in the dump's log section + the syslog) and echoes to the debug UART. Both
 * paths are bounded and skip any RTOS signalling, so they are fault-path safe.
 */
static void CrashGlue_consoleWrite(const char *data, uint32_t len, void *ctx)
{
    (void)ctx;
    LogBuffer::writeNoNotify(reinterpret_cast<const uint8_t*>(data),
                             static_cast<uint16_t>(len));
    Driver::LogUart::WriteRaw(data, len);
}

void CrashGlue_setup(void)
{
    const uint32_t resetFlags = Driver::System::GetResetFlagsRaw();

    CrashDumpConfig cfg = {};
    cfg.projectName   = Driver::System::GetProjectName();
    cfg.buildVersion  = Driver::System::GetBuildVersionStr();
    cfg.bootVersion   = Driver::System::GetBootVersionStr();
    cfg.libcVersion   = Driver::System::GetLibcVersionStr();
    cfg.flashStart    = (uint32_t)&__flash_start__;
    cfg.flashEnd      = (uint32_t)&__flash_end__;
    cfg.ramStart      = (uint32_t)&__ram1_start__;
    cfg.ramEnd        = (uint32_t)&__ram1_end__;
    cfg.resetFlags    = resetFlags;
    cfg.watchdogReset = (resetFlags & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) != 0u;
    cfg.nowMs         = CrashGlue_nowMs;
    cfg.logProvider   = CrashGlue_logProvider;
    cfg.logCtx        = nullptr;
    cfg.rtos.taskSnapshot = CrashGlue_taskSnapshot;
    cfg.rtos.currentStack = CrashGlue_currentStack;
    cfg.rtos.ctx          = nullptr;
    cfg.sink.open     = CrashGlue_sinkOpen;
    cfg.sink.write    = CrashGlue_sinkWrite;
    cfg.sink.close    = CrashGlue_sinkClose;
    cfg.sink.ctx      = nullptr;
    cfg.onSaved       = CrashGlue_onSaved;
    cfg.onSavedCtx    = nullptr;
    cfg.consoleWrite  = CrashGlue_consoleWrite;
    cfg.consoleWriteCtx = nullptr;
    // Static storage updated in place by the monitor's periodic check; the
    // capture path copies it, so a capsule's heap figures are up to one
    // check period (HeapStackMonitor::Start in main.cpp) stale.
    cfg.heapSample    = HeapStackMonitor::GetHeapSample();

    CrashDump_init(&cfg);
}
