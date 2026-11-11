/**
 ******************************************************************************
 * @file    FIFO.c
 * @brief   FIFO component.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "FIFO/FIFO.h"

#include <string.h>

////////////////////////////////////////////////////////////////////////////////
/// Log
////////////////////////////////////////////////////////////////////////////////

#define LOG_MODULE_PRX      "FIFO"
#define LOG_MODULE_LEVEL    LOG_LEVEL_ERROR
#include "Logger.h"

////////////////////////////////////////////////////////////////////////////////
/// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

static void FIFO_resetI(IFIFO_t* f)
{
    FIFO_reset((FIFO_t*)f->ctx);
}

static bool FIFO_pushI(IFIFO_t* f, uint8_t value)
{
    return FIFO_push((FIFO_t*)f->ctx, value);
}

static bool FIFO_pushBufferI(IFIFO_t* f, uint8_t *buff, uint32_t size)
{
    return FIFO_pushBuffer((FIFO_t*)f->ctx, buff, size);
}

static uint8_t FIFO_popI(IFIFO_t* f)
{
    return FIFO_pop((FIFO_t*)f->ctx);
}

static bool FIFO_popBufferI(IFIFO_t* f, uint8_t *buf, uint32_t size)
{
    return FIFO_popBuffer((FIFO_t*)f->ctx, buf, size);
}

static uint32_t FIFO_getAvailForReadI(IFIFO_t* f)
{
    return FIFO_getAvailForRead((FIFO_t*)f->ctx);
}

static uint32_t FIFO_getAvailForWriteI(IFIFO_t* f)
{
    return FIFO_getAvailForWrite((FIFO_t*)f->ctx);
}

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initializes FIFO
 * 
 * @param f       Pointer to the FIFO object
 * @param buff    Pointer to the work buffer
 * @param maxSize The work buffer size
 */
void FIFO_init(FIFO_t* f, uint8_t *buff, uint32_t maxSize)
{
    memset((uint8_t *)f, 0, sizeof(FIFO_t));

    f->buffer  = buff;
    f->maxSize = maxSize;

    f->intf.reset            = FIFO_resetI;
    f->intf.push             = FIFO_pushI;
    f->intf.pushBuffer       = FIFO_pushBufferI;
    f->intf.pop              = FIFO_popI;
    f->intf.popBuffer        = FIFO_popBufferI;
    f->intf.getAvailForRead  = FIFO_getAvailForReadI;
    f->intf.getAvailForWrite = FIFO_getAvailForWriteI;
    f->intf.ctx              = (void*) f;
}

/**
 * @brief Resets FIFO
 * 
 * @param f Pointer to the FIFO object
 */
void FIFO_reset(FIFO_t* f)
{
    f->rdIdx = f->wrIdx = f->availForRead = 0;
}

/**
 * @brief Push byte into the FIFO
 * 
 * @param  f     Pointer to the FIFO object
 * @param  value Value that will be pushed into the FIFO
 * @return       true if OK, otherwise false
 */
bool FIFO_push(FIFO_t* f, uint8_t value)
{
    f->buffer[f->wrIdx] = value;

    f->wrIdx++;
    if (f->wrIdx >= f->maxSize)
        f->wrIdx = 0;

    f->availForRead++;
    if (f->availForRead > f->maxSize) {
        LOG_ERROR("%s - everloaded\n", __FUNCTION__);
        
        f->availForRead = f->maxSize;

        f->rdIdx++;
        if (f->rdIdx >= f->maxSize)
            f->rdIdx = 0;
    }

    return true;
}

/**
 * @brief Pushes whole buffer into the FIFO
 * 
 * @param  f    Pointer to the FIFO object
 * @param  buf  Buffer that will be pushed into the FIFO
 * @param  size Buffer size
 * @return      true if OK, otherwise false
 */
bool FIFO_pushBuffer(FIFO_t* f, const uint8_t *buf, uint32_t size)
{
    if (f->maxSize - f->availForRead < size) {
        LOG_ERROR("%s - there is no free space\n", __FUNCTION__);
        LOG_WARNING("f->maxSize      = %d\n", f->maxSize);
        LOG_WARNING("f->availForRead = %d\n", f->availForRead);
        LOG_WARNING("input length    = %d\n", size);
        return false;
    }

    if (f->wrIdx >= f->rdIdx) {
        ////////////////////////////////////
        //           avail
        // |______RD*******WR______|
        //    W1                W2 
        //
        ////////////////////////////////////

        /// W1 buffer writting
        uint32_t W2Size = f->maxSize - f->wrIdx;
        if (W2Size > size) {
            W2Size = size;
        }

        memcpy(&f->buffer[f->wrIdx], buf, W2Size);

        f->wrIdx += W2Size;
        if (f->wrIdx >= f->maxSize) {
            f->wrIdx = 0;
        }

        f->availForRead += W2Size;
        buf             += W2Size;
        size            -= W2Size;

        /// W2 buffer writting
        if (size != 0) {
            memcpy(&f->buffer[0], buf, size);

            f->wrIdx         = size;
            f->availForRead += size;
        }
    } else {
        ////////////////////////////////////
        //   avail1              avail2
        // |********WR________RD********|
        //               W1
        //
        ////////////////////////////////////

        /// W1 buffer writting
        memcpy(&f->buffer[f->wrIdx], buf, size);

        f->wrIdx += size;
        if (f->wrIdx >= f->maxSize) {
            f->wrIdx = 0;
        }

        f->availForRead += size;
    }

    return true;
}

/**
 * @brief Pops value from the FIFO
 * 
 * @param  f Pointer to the FIFO object
 * @return   uint8_t 
 */
uint8_t FIFO_pop(FIFO_t* f)
{
    if (f->availForRead == 0) {
        LOG_ERROR("%s - there is no data\n", __FUNCTION__);
        return false;
    }

    uint8_t result = f->buffer[f->rdIdx++];

    if (f->rdIdx >= f->maxSize) {
        f->rdIdx = 0;
    }

    --f->availForRead;

    return result;
}

/**
 * @brief Pops whole buffer from the FIFO
 * 
 * @param  f    Pointer to the FIFO object
 * @param  buf  Buffer that will be popped from the FIFO
 * @param  size Buffer size
 * @return      true if OK, otherwise false
 */
bool FIFO_popBuffer(FIFO_t* f, uint8_t *buf, uint32_t size)
{
    if (size == 0) {
        return true;
    }
    
    if (size > f->availForRead) {
        LOG_ERROR("%s - there is not enough data\n", __FUNCTION__);
        return false;
    }

    if (f->rdIdx >= f->wrIdx) {
        ////////////////////////////////////
        //   avail2              avail1
        // |********WR________RD********|
        //     R2                  R1
        ////////////////////////////////////

        /// R1 buffer reading
        uint32_t R1Size = f->maxSize - f->rdIdx;
        if (R1Size > size) {
            R1Size = size;
        }

        memcpy(buf, &f->buffer[f->rdIdx], R1Size);

        f->rdIdx += R1Size;
        if (f->rdIdx >= f->maxSize) {
            f->rdIdx = 0;
        }

        f->availForRead -= R1Size;
        buf             += R1Size;
        size            -= R1Size;

        /// R2 buffer reading
        if (size != 0) {
            memcpy(buf, &f->buffer[0], size);

            f->rdIdx         = size;
            f->availForRead -= size;
        }
    } else {
        ////////////////////////////////////
        //           avail
        // |______RD*******WR______|
        //             R1
        ////////////////////////////////////

        memcpy(&buf[0], &f->buffer[f->rdIdx], size);

        f->rdIdx += size;
        if (f->rdIdx >= f->maxSize) {
            f->rdIdx = 0;
        }

        f->availForRead -= size;
    }

    return true;
}

/**
 * @brief Returns number of bytes available for reading by the pop function
 * 
 * @param  f Pointer to the FIFO object
 * @return   uint32_t 
 */
uint32_t FIFO_getAvailForRead(FIFO_t* f)
{
    return f->availForRead;
}

/**
 * @brief Returns number of bytes available for writting by the push functions
 * 
 * @param  f Pointer to the FIFO object
 * @return   uint32_t 
 */
uint32_t FIFO_getAvailForWrite(FIFO_t* f)
{
    return f->maxSize - f->availForRead;
}
