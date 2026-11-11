/**
 ******************************************************************************
 * @file    LinkedList.h
 * @brief   Linked list component.
 *          
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __LinkedList_h__
#define __LinkedList_h__

#ifdef __cplusplus
 extern "C" {
#endif

#include "Porting.h"

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

typedef struct LinkedListNode {
    void*                 item;
    struct LinkedListNode* next;
}LinkedListNode_t;

typedef struct {
    LinkedListNode_t* first;
    LinkedListNode_t* last;
    uint16_t          count;
} LinkedList_t;

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

void              LinkedList_init(LinkedList_t* list);
void              LinkedList_clear(LinkedList_t* list);
uint32_t          LinkedList_getCount(LinkedList_t* list);
LinkedListNode_t* LinkedList_getRoot(LinkedList_t* list);
bool              LinkedList_insItem(LinkedList_t* list, uint32_t pos, void* item);
bool              LinkedList_addItem(LinkedList_t* list, const void* item);
void*             LinkedList_getItem(LinkedList_t* list, uint32_t i);
void              LinkedList_delete(LinkedList_t* list, const void* item);
void              LinkedList_remove(LinkedList_t* list, uint32_t i);
bool              LinkedList_swap(LinkedList_t* list, uint32_t i, uint32_t j);
int32_t           LinkedList_indexOf(LinkedList_t* list, const void* item);
LinkedListNode_t* LinkedList_nodeOf(LinkedList_t* list, const void* item);

#ifdef __cplusplus
 }
#endif

#endif
