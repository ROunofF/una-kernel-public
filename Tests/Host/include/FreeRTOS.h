/**
 * @file FreeRTOS.h
 * @brief Host stand-in. Kernel sources include this for configASSERT.
 *
 * The host build has no RTOS; cmsis_os2.h carries the small slice of the API
 * the code under test actually calls, and configASSERT maps to assert().
 */
#ifndef FREERTOS_H
#define FREERTOS_H

#include "cmsis_os2.h"

#endif // FREERTOS_H
