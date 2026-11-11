/**
 ******************************************************************************
 * @file    freertos_tasks_c_additions.h
 * @brief   xTaskSnapshotAll implementation, injected into tasks.c.
 *
 * FreeRTOS includes this file at the very end of tasks.c when
 * configINCLUDE_FREERTOS_TASK_C_ADDITIONS_H == 1, so the code below has access
 * to the kernel internals (TCB_t, the scheduler lists, prvTaskCheckFreeStackSpace)
 * without modifying the vendored source. Do not include it anywhere else.
 ******************************************************************************
 */

#ifndef FREERTOS_TASKS_C_ADDITIONS_H
#define FREERTOS_TASKS_C_ADDITIONS_H

#include "crash_task_snapshot.h"

/* Read-only walk of a single task list into the snapshot array. Does not take a
 * lock or mutate the list; a guard bounds the walk so a corrupted list cannot
 * loop forever when called from a fault handler. */
static void prvSnapshotTaskList( const List_t *   pxList,
                                 eTaskState       eState,
                                 TaskSnapshot_t * pxArray,
                                 UBaseType_t      uxArraySize,
                                 UBaseType_t *    puxCount )
{
    if( pxList == NULL )
    {
        return;
    }

    const ListItem_t * pxEnd  = listGET_END_MARKER( pxList );
    const ListItem_t * pxItem = listGET_HEAD_ENTRY( pxList );
    UBaseType_t        uxGuard = uxCurrentNumberOfTasks + 1U;

    while( ( pxItem != pxEnd ) &&
           ( *puxCount < uxArraySize ) &&
           ( uxGuard-- > 0U ) )
    {
        TCB_t * pxTCB = ( TCB_t * ) listGET_LIST_ITEM_OWNER( pxItem );

        if( pxTCB != NULL )
        {
            TaskSnapshot_t * p = &pxArray[ *puxCount ];

            p->pvHandle     = ( void * ) pxTCB;
            p->pcName       = ( const char * ) pxTCB->pcTaskName;
            p->ulTopOfStack = ( uint32_t ) pxTCB->pxTopOfStack;
            p->ulStackBase  = ( uint32_t ) pxTCB->pxStack;
            p->ulState      = ( uint32_t ) ( ( pxTCB == pxCurrentTCB ) ? eRunning : eState );
            p->ulPriority   = ( uint32_t ) pxTCB->uxPriority;
            #if ( ( configUSE_TRACE_FACILITY == 1 ) || ( INCLUDE_uxTaskGetStackHighWaterMark == 1 ) )
                p->ulHighWater = ( uint32_t ) prvTaskCheckFreeStackSpace( ( const uint8_t * ) pxTCB->pxStack );
            #else
                p->ulHighWater = 0U;
            #endif

            ( *puxCount )++;
        }

        pxItem = listGET_NEXT( pxItem );
    }
}

UBaseType_t xTaskSnapshotAll( TaskSnapshot_t * pxArray, UBaseType_t uxArraySize )
{
    UBaseType_t uxCount = 0;
    UBaseType_t uxQueue;

    if( ( pxArray == NULL ) || ( uxArraySize == 0U ) )
    {
        return 0;
    }

    /* Ready tasks, highest priority first. */
    uxQueue = configMAX_PRIORITIES;
    do
    {
        uxQueue--;
        prvSnapshotTaskList( &( pxReadyTasksLists[ uxQueue ] ), eReady, pxArray, uxArraySize, &uxCount );
    } while( uxQueue > ( UBaseType_t ) tskIDLE_PRIORITY );

    /* Blocked (delayed) tasks. */
    prvSnapshotTaskList( ( const List_t * ) pxDelayedTaskList,         eBlocked, pxArray, uxArraySize, &uxCount );
    prvSnapshotTaskList( ( const List_t * ) pxOverflowDelayedTaskList, eBlocked, pxArray, uxArraySize, &uxCount );

    #if ( INCLUDE_vTaskSuspend == 1 )
        prvSnapshotTaskList( &xSuspendedTaskList, eSuspended, pxArray, uxArraySize, &uxCount );
    #endif
    #if ( INCLUDE_vTaskDelete == 1 )
        prvSnapshotTaskList( &xTasksWaitingTermination, eDeleted, pxArray, uxArraySize, &uxCount );
    #endif

    return uxCount;
}

#endif /* FREERTOS_TASKS_C_ADDITIONS_H */
