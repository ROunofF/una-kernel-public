#ifndef __TimerSysMs_h__
#define __TimerSysMs_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

////////////////////////////////////////////////////////////////////////////////
/// Defines
////////////////////////////////////////////////////////////////////////////////

#define TIMER_SYS_MS_SECONDS(v)     (1000UL * (v))  
#define TIMER_SYS_MS_MINUTES(v)     (TIMER_SYS_MS_SECONDS(1) * 60 * (v))  

////////////////////////////////////////////////////////////////////////////////
/// Global variables
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

typedef uint32_t (*ITimerSysMs_getTicks_t)(void);

typedef struct
{
    bool     active;
    bool     complete;
    uint32_t interval;
    uint32_t prevSysTimer;
} TimerSysMs_t;

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

void TimerSysMs_init(ITimerSysMs_getTicks_t cb);

void     TimerSysMs_setActive(TimerSysMs_t* timer, bool value);
bool     TimerSysMs_getActive(TimerSysMs_t* timer);
void     TimerSysMs_setInterval(TimerSysMs_t* timer, uint32_t value);
uint32_t TimerSysMs_getInterval(TimerSysMs_t* timer);
void     TimerSysMs_start(TimerSysMs_t* timer);
void     TimerSysMs_startWithValue(TimerSysMs_t* timer, uint32_t value);
void     TimerSysMs_stop(TimerSysMs_t* timer);
void     TimerSysMs_reset(TimerSysMs_t* timer);
void     TimerSysMs_wait(uint32_t ms_number);
uint32_t TimerSysMs_getTicks(void);
uint32_t TimerSysMs_getTime(TimerSysMs_t* timer);
bool     TimerSysMs_check(TimerSysMs_t* timer);
uint32_t TimerSysMs_passedNow(uint32_t now, uint32_t time_stamp);
uint32_t TimerSysMs_passed(uint32_t time_stamp);

#ifdef __cplusplus
 }
#endif

#endif /*__TIMER_SYS_MS_H__*/
