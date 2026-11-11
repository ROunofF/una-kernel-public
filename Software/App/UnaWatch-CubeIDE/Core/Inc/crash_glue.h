/**
 ******************************************************************************
 * @file    crash_glue.h
 * @brief   Wires the portable CrashDump module to this project's services.
 *
 * The CrashDump module itself depends only on CMSIS-Core; this glue supplies
 * the project-specific ports (identity, memory ranges, reset cause, time, log
 * provider, FreeRTOS introspection, FatFs persistence) via CrashDump_init.
 ******************************************************************************
 */

#ifndef CRASH_GLUE_H
#define CRASH_GLUE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the CrashDump config from this project and initialize the module.
 *
 * Call once, early in boot (after the system/reset state is known, before a
 * fault can occur). The persistence sink is only invoked later, by
 * CrashDump_check(), once the file system is mounted.
 */
void CrashGlue_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* CRASH_GLUE_H */
