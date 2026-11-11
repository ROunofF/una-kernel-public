/**
 ******************************************************************************
 * @file    crash_task_snapshot.h
 * @brief   Fault-safe FreeRTOS task snapshot (public declaration).
 *
 * The implementation lives in freertos_tasks_c_additions.h, pulled into tasks.c
 * via the official configINCLUDE_FREERTOS_TASK_C_ADDITIONS_H hook (no edits to
 * the vendored FreeRTOS source). Include this header wherever the snapshot is
 * consumed (e.g. the CrashDump RTOS glue).
 ******************************************************************************
 */

#ifndef CRASH_TASK_SNAPSHOT_H
#define CRASH_TASK_SNAPSHOT_H

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One task's identity and stack info. pcName points into the live TCB and is
 * only valid until the scheduler runs again; copy it out immediately.
 */
typedef struct {
    void *       pvHandle;     /* TCB pointer. */
    const char * pcName;       /* Task name (points into the TCB). */
    uint32_t     ulTopOfStack; /* Saved stack pointer (TCB->pxTopOfStack). */
    uint32_t     ulStackBase;  /* Stack region start (TCB->pxStack). */
    uint32_t     ulState;      /* eTaskState (eRunning for the active task). */
    uint32_t     ulPriority;   /* Current priority. */
    uint32_t     ulHighWater;  /* Free stack words remaining (high-water). */
} TaskSnapshot_t;

/*
 * Walk every scheduler task list and fill pxArray with up to uxArraySize task
 * snapshots. Intended for crash/fault context: takes no critical section,
 * suspends nothing, does not mutate the lists, so it is safe to call with
 * interrupts disabled. Best-effort on a corrupted list (a per-list iteration
 * guard bounds the walk). Returns the number of records stored.
 */
UBaseType_t xTaskSnapshotAll( TaskSnapshot_t * pxArray, UBaseType_t uxArraySize );

#ifdef __cplusplus
}
#endif

#endif /* CRASH_TASK_SNAPSHOT_H */
