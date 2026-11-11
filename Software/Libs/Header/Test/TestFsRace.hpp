/**
 ******************************************************************************
 * @file    TestFsRace.hpp
 * @brief   Repro for the FatFs cross-volume SysLock reentrancy race.
 ******************************************************************************
 */

#ifndef TEST_FS_RACE_HPP
#define TEST_FS_RACE_HPP

/**
 * @brief Reproduce the FatFs reentrancy race on the global SysLock flag.
 *
 * Spawns two equal-priority tasks on two different volumes:
 *  - a writer on 2:/ that issues only syslock=0 calls (f_lseek/f_write/f_sync);
 *  - a path-op task on 1:/ that issues syslock=1 calls (f_open/f_close), which
 *    take the shared system mutex and set the global SysLock=2 for the op.
 *
 * Because the two volumes use different per-volume mutexes, the tasks never
 * serialize each other. When a tick preempts the path task while it holds the
 * system mutex (SysLock==2), the writer's unlock_volume samples that global
 * flag and releases the system mutex it never took, tripping the FreeRTOS
 * xTaskPriorityDisinherit assert (configASSERT(pxTCB == pxCurrentTCB)).
 */
void TestFsRace_execute(void);

#endif // TEST_FS_RACE_HPP
