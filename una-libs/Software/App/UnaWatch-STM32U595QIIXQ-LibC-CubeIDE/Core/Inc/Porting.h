#ifndef __Porting_h__
#define __Porting_h__

#ifdef __cplusplus
 extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define DISABLE_INTERRUPT()       __disable_irq()
#define ENABLE_INTERRUPT()        __enable_irq()   

#define WAIT(ms)                  HAL_Delay(ms)
#define TICKS()                   HAL_GetTick()

#define GET_SYS_TICK()          HAL_GetTick()
#define GET_PASSED(TimeStamp)   (HAL_GetTick() - (TimeStamp))

#define MIN(A, B) (((A) < (B)) ? (A) : (B))
#define MAX(A, B) (((A) > (B)) ? (A) : (B))

#define SATURATION(N, L, H) (((N)<(L))?(L):(((N)>(H))?(H):(N)))

typedef void (*notify_cb_t)(void);
typedef void (*notify_arg_cb_t)(void* arg);

typedef union
{
    uint8_t  b[4];
    uint16_t i[2];
    uint32_t ul;
    int32_t  l;
    float    f;
} union32_t;

typedef union
{
    uint8_t  b[2];
    uint16_t i;
} union16_t;

typedef union
{
    uint8_t  b[8];
    uint16_t i[4];
    uint32_t ul[2];
    uint64_t ui64;
    double   d;
} union64_t;

#define ARRAY_SIZE(A)           (sizeof(A) / sizeof(A[0]))

////////////////////////////////////////////////////////////////////////////////
// Debugging config
////////////////////////////////////////////////////////////////////////////////

void __assert_trap(const char* f, int line);

#ifdef DEBUG
    #define ASSERT(cond)    do {                                        \
                                if ((cond) == false) {                  \
                                    __assert_trap(__FILE_NAME__, __LINE__);  \
                                }                                       \
                            } while (0)                                 \

#else
    #define ASSERT(cond)

#endif

#ifdef __cplusplus
 }
#endif

#endif /* __Porting_h__ */
