/**
 ******************************************************************************
 * @file    IFIFO.h
 * @brief   This is the FIFO interface.
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __IFIFO_hpp__
#define __IFIFO_hpp__

#include "Porting.h"

typedef struct IFIFOStruct IFIFO_t;

typedef void     (*IFIFO_reset_t)(IFIFO_t* f);
typedef bool     (*IFIFO_push_t)(IFIFO_t* f, uint8_t value);
typedef bool     (*IFIFO_pushBuffer_t)(IFIFO_t* f, uint8_t *buff, uint32_t size);
typedef uint8_t  (*IFIFO_pop_t)(IFIFO_t* f);
typedef bool     (*IFIFO_popBuffer_t)(IFIFO_t* f, uint8_t *buf, uint32_t size);
typedef uint32_t (*IFIFO_getAvailForRead_t)(IFIFO_t* f);
typedef uint32_t (*IFIFO_getAvailForWrite_t)(IFIFO_t* f);

struct IFIFOStruct {
    IFIFO_reset_t            reset;
    IFIFO_push_t             push;
    IFIFO_pushBuffer_t       pushBuffer;
    IFIFO_pop_t              pop;
    IFIFO_popBuffer_t        popBuffer;
    IFIFO_getAvailForRead_t  getAvailForRead;
    IFIFO_getAvailForWrite_t getAvailForWrite;

    void* ctx;
};

#endif
