/**
 ******************************************************************************
 * @file    LinkedList.c
 * @brief   Linked list component.
 *          
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "LinkedList/LinkedList.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

static void PostDelete(LinkedList_t* list, LinkedListNode_t *prev, LinkedListNode_t *node)
{
    if (node == list->first) {
        list->first = node->next;

        if (list->last == node) {
            list->last = NULL;
        }
    }
    else {
        prev->next = node->next;

        if (list->last == node) {
            list->last = prev;
        }
    }

    free(node);

    list->count--;
}

/**
 * @brief Returns pointer to the item by its index in the list
 *
 * @param list Pointer to a linked list object
 * @param i    Index in the list
 * @return     Pointer to the item
 */
static LinkedListNode_t* LinkedList_getNode(LinkedList_t* list, uint32_t i)
{
    uint32_t          index;
    LinkedListNode_t* node;

    if (i >= list->count) {
        return NULL;
    }

    node = list->first;
    for (index = 0; index < i; ++index) {
        node = node->next;
    }

    return node;
}

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief List initialization
 * 
 * @param list Pointer to a linked list object
 */
void LinkedList_init(LinkedList_t* list)
{
    memset(list, 0, sizeof(LinkedList_t));
}

/**
 * @brief Clears the list
 * 
 * @param list Pointer to a linked list object
 */
void LinkedList_clear(LinkedList_t* list)
{
    while (LinkedList_getCount(list) != 0) {
        LinkedList_remove(list, 0);
    }
}

/**
 * @brief Returns number of items in the list
 * 
 * @param list Pointer to a linked list object
 * @return     Number of items 
 */
uint32_t LinkedList_getCount(LinkedList_t* list)
{
    return list->count;
}

/**
 * @brief Adds the new item into the list 
 * 
 * @param list Pointer to a linked list object
 * @param item Pointer to the new item
 * @return     True if added sucessfuly, otherwise false
 */
bool LinkedList_addItem(LinkedList_t* list, const void* item)
{
    LinkedListNode_t* node = (LinkedListNode_t*) malloc(sizeof(LinkedListNode_t));

    if (node == NULL) {
        return false;
    }

    node->next = NULL;

    if (list->count == 0) {
        list->first = node;
        list->last  = node;
    } else {
        list->last->next = node;
        list->last = node;
    }

    list->count++;

    node->item = (void*) item;

    return true;
}

/**
 * @brief Inserts the new item into the specified position in the list 
 * 
 * @param list Pointer to a linked list object
 * @param pos  Position in the list
 * @param item Pointer to the new item
 * @return     True if inserted sucessfuly, otherwise false
 */
bool LinkedList_insItem(LinkedList_t* list, uint32_t pos, void* item)
{
    if ((list->count == 0) && (pos == 0)) {
        return LinkedList_addItem(list, item);
    }

    if (pos >= list->count) {
        return false;
    }

    uint32_t idx = 0;    
    LinkedListNode_t* prevNode = NULL;
    LinkedListNode_t* node     = list->first;
    while (node != NULL) {
        if (idx == pos) {
            LinkedListNode_t* newNode = (LinkedListNode_t*) malloc(sizeof(LinkedListNode_t));
            assert(newNode != NULL);

            if (prevNode != NULL) {
                prevNode->next = newNode;
            } else {
                list->first = newNode;
            }

            newNode->next = node;
            newNode->item = item;

            if (list->last == node) {
                list->last = newNode;
            }

            ++list->count;

            return true;
        }
        
        prevNode = node;

        ++idx;
        node = node->next;
    }

    return false;
}

/**
 * @brief Returns pointer to the item by its index in the list
 * 
 * @param list Pointer to a linked list object
 * @param i    Index in the list
 * @return     Pointer to the item
 */
void* LinkedList_getItem(LinkedList_t* list, uint32_t i)
{
    LinkedListNode_t* node = LinkedList_getNode(list, i);

    if (node == NULL) {
        return NULL;
    }

    return (void*) node->item;
}

/**
 * @brief Deletes item from the list
 * 
 * @param list Pointer to a linked list object
 * @param item Pointer to the item
 */
void LinkedList_delete(LinkedList_t* list, const void *item)
{
    uint32_t          index;
    LinkedListNode_t* node;
    LinkedListNode_t* prev;

    prev = NULL;
    node = list->first;

    for (index = 0; index < list->count; ++index) {
        if (node->item == item) {
            break;
        }

        prev = node;
        node = node->next;
    }

    if (index >= list->count) {
        return;
    }

    PostDelete(list, prev, node);
}

/**
 * @brief Removes item by its index
 * 
 * @param list Pointer to a linked list object
 * @param i    Item index in the list
 */
void LinkedList_remove(LinkedList_t* list, uint32_t i)
{
    uint32_t          index;
    LinkedListNode_t* node;
    LinkedListNode_t* prev;

    if (i >= list->count) {
        return;
    }

    prev = NULL;
    node = list->first;

    for (index = 0; index < i; ++index) {
        prev = node;
        node = node->next;
    }

    PostDelete(list, prev, node);
}

bool LinkedList_swap(LinkedList_t* list, uint32_t i, uint32_t j)
{
    LinkedListNode_t* nodeI = LinkedList_getNode(list, i);
    LinkedListNode_t* nodeJ = LinkedList_getNode(list, j);

    if (nodeI == NULL) {
        return false;
    }

    if (nodeJ == NULL) {
        return false;
    }

    void* pocket = nodeI->item;
    nodeI->item  = nodeJ->item;
    nodeJ->item  = pocket;

    return true;
}

/**
 * @brief Returns index of the item in the list
 * 
 * @param list Pointer to a linked list object
 * @param item Pointer to the item
 * @return     Index of the item in the list 
 */
int32_t LinkedList_indexOf(LinkedList_t* list, const void* item)
{
    LinkedListNode_t* node = LinkedList_getRoot(list);
    int32_t           idx  = 0;

    while (node != NULL) {
        if (node->item == item) {
            return idx;
        }

        ++idx;

        node = node->next;
    }

    return -1;
}

/**
 * @brief Looks up a node in the list and returns a pointer to it.
 * 
 * @param list Pointer to a linked list object
 * @param item Pointer to the item
 * @return     Pointer to the list node
 */
LinkedListNode_t* LinkedList_nodeOf(LinkedList_t* list, const void* item)
{
    for (LinkedListNode_t* node = LinkedList_getRoot(list); node != NULL; node = node->next) {
        if (node->item == item) {
            return node;
        } 
    }

    return NULL;
}

/**
 * @brief Returns pointer to the root node of the list
 * 
 * @param list Pointer to a linked list object
 * @return     Pointer to the root node
 */
LinkedListNode_t* LinkedList_getRoot(LinkedList_t* list)
{
    return list->first;
}
