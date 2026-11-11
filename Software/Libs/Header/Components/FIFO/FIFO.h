/**
 ******************************************************************************
 * @file    FIFO.h
 * @brief   FIFO component.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __FIFO_h__
#define __FIFO_h__

#ifdef __cplusplus
 extern "C" {
#endif

#include "Porting.h"
#include "Interfaces/Components/IFIFO.h"

////////////////////////////////////////////////////////////////////////////////
/// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

typedef struct {
    uint8_t* buffer;
    uint32_t maxSize;
    uint32_t rdIdx;
    uint32_t wrIdx;
    uint32_t availForRead;

    IFIFO_t intf;
} FIFO_t;

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

void     FIFO_init(FIFO_t* f, uint8_t *buff, uint32_t maxSize);
void     FIFO_reset(FIFO_t* f);
bool     FIFO_push(FIFO_t* f, uint8_t value);
bool     FIFO_pushBuffer(FIFO_t* f, const uint8_t *buff, uint32_t size);
uint8_t  FIFO_pop(FIFO_t* f);
bool     FIFO_popBuffer(FIFO_t* f, uint8_t *buf, uint32_t size);
uint32_t FIFO_getAvailForRead(FIFO_t* f);
uint32_t FIFO_getAvailForWrite(FIFO_t* f);

#ifdef __cplusplus
 }
#endif

#endif
