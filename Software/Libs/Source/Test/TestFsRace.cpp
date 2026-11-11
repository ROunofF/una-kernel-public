#define LOG_MODULE_PRX      "FsRace"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestFsRace.hpp"
#include "OS.hpp"
#include "ff.h"

#include "FreeRTOS.h"
#include "task.h"

namespace {

// Two different volumes on the single eMMC. Different volumes mean different
// FatFs per-volume mutexes, so the two tasks below never serialize each other.
// That overlap is exactly what lets them race on the global SysLock flag.
constexpr char kWriterDir[]  = "2:/fsrace";
constexpr char kWriterPath[] = "2:/fsrace/writer.bin";
constexpr char kProbeDir[]   = "0:/fsrace";
constexpr char kProbePath[]  = "0:/fsrace/probe.txt";

// Equal priority so the 1024 Hz tick round-robins the two tasks; a tick can
// then land while the path task holds the system mutex. Well clear of idle.
constexpr UBaseType_t kTaskPrio = tskIDLE_PRIORITY + 5;

// 6 KiB stacks: deliberately generous so a stack overflow cannot masquerade as
// the assert we are hunting (the real race leaves the stack half empty).
constexpr uint16_t kStackWords = 1536;

// Periodically back off so lower-priority housekeeping (the watchdog feed in
// the idle hook) gets the CPU. Two tight equal-priority FS loops otherwise
// starve it and the watchdog resets the MCU, masking the actual test result.
constexpr uint32_t kYieldPeriodMs = 10000;
constexpr uint32_t kYieldMs       = 200;

FIL          sWriterFile;
TaskHandle_t sWriterTask;
TaskHandle_t sPathTask;

volatile uint32_t sWriterIters;
volatile uint32_t sPathIters;

// Volume 2: only syslock=0 operations. Their unlock_volume still consults the
// global SysLock and may give the system mutex this task never took.
void writerTask(void*)
{
    FRESULT fr = f_open(&sWriterFile, kWriterPath, FA_WRITE | FA_OPEN_ALWAYS);
    if (fr != FR_OK) {
        LOG_ERROR("writer f_open '%s' failed: %d\n", kWriterPath, (int)fr);
        vTaskDelete(nullptr);
    }

    static const char rec[] = "fsrace-writer-record\n";
    TickType_t lastYield = xTaskGetTickCount();
    for (;;) {
        UINT bw = 0;
        f_lseek(&sWriterFile, 0);                   // keep the file tiny
        f_write(&sWriterFile, rec, sizeof(rec) - 1, &bw);
        f_sync(&sWriterFile);                        // unlock_volume samples SysLock
        if ((++sWriterIters % 20000U) == 0) {
            LOG_INFO("alive: writer=%u path=%u\n", sWriterIters, sPathIters);
        }
        const TickType_t now = xTaskGetTickCount();
        if ((now - lastYield) >= pdMS_TO_TICKS(kYieldPeriodMs)) {
            OS::Delay(kYieldMs);
            lastYield = now;
        }
    }
}

// Volume 1: syslock=1 operations (f_open via mount_volume). Each open takes the
// shared system mutex and sets the global SysLock=2 for the op duration.
void pathOpTask(void*)
{
    TickType_t lastYield = xTaskGetTickCount();
    for (;;) {
        FIL probe;
        FRESULT fr = f_open(&probe, kProbePath, FA_READ | FA_OPEN_EXISTING);
        if (fr == FR_OK) {
            f_close(&probe);
        }
        ++sPathIters;
        const TickType_t now = xTaskGetTickCount();
        if ((now - lastYield) >= pdMS_TO_TICKS(kYieldPeriodMs)) {
            OS::Delay(kYieldMs);
            lastYield = now;
        }
    }
}

}  // namespace

void TestFsRace_execute(void)
{
    LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
    LOG_INFO_WP("//// FatFs cross-volume SysLock race repro\n");
    LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
    LOG_INFO("Expect a configASSERT in xTaskPriorityDisinherit (tasks.c): a 2:/\n");
    LOG_INFO("syslock=0 op releases the system mutex held by a 1:/ syslock=1 op.\n");

    // Working dirs and the probe file (ignore 'already exists' / errors).
    f_mkdir(kWriterDir);
    f_mkdir(kProbeDir);
    {
        FIL f;
        if (f_open(&f, kProbePath, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            f_close(&f);
        }
    }

    if (xTaskCreate(writerTask, "fsraceW", kStackWords, nullptr, kTaskPrio, &sWriterTask) != pdPASS ||
        xTaskCreate(pathOpTask, "fsraceS", kStackWords, nullptr, kTaskPrio, &sPathTask) != pdPASS) {
        LOG_ERROR("task create failed\n");
    }

    for (;;) {
        OS::Delay(5000);
    }
}
