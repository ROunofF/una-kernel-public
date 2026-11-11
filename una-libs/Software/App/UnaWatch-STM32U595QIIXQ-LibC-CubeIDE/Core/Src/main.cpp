/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#include "Porting.h"
#include "Libc/ImageMaker.h"

#include <stdint.h>
#include <stdio.h>

typedef struct {
    char version[32];
    char date[16];
    char time[16];
    char vendor[32];
    char reserved[32];
} SharedLibInfo_t;

extern "C" const SharedLibInfo_t __attribute__((section (".shared_lib_info_section"))) shared_lib_info __attribute__((used)) = {
    {   "sl." BUILD_VERSION },
    {   __DATE__            },
    {   __TIME__            },
    {   PROJECT_NAME        }
};

////////////////////////////////////////////////////////////
//// External symbols that the linker must provide
////////////////////////////////////////////////////////////

extern uint32_t _sidata; //// Start address of initialized data in flash
extern uint32_t _sdata;  //// Start of the .data section in RAM
extern uint32_t _edata;  //// End of the .data section in RAM
extern uint32_t _sbss;   //// Start of the .bss section
extern uint32_t _ebss;   //// End of the .bss section

////////////////////////////////////////////////////////////
//// Call C++ constructors
////////////////////////////////////////////////////////////

extern "C" void __libc_init_array(void);

#ifdef __cplusplus
  extern "C" {
#endif

extern void (*__preinit_array_start []) (void) __attribute__((weak));
extern void (*__preinit_array_end [])   (void) __attribute__((weak));
extern void (*__init_array_start [])    (void) __attribute__((weak));
extern void (*__init_array_end [])      (void) __attribute__((weak));
extern void (*__fini_array_start[])     (void) __attribute__((weak));
extern void (*__fini_array_end[])       (void) __attribute__((weak));

/* Iterate over all the init routines.  */
void __una_fini_array(void)
{
    int count = __fini_array_end - __fini_array_start;
    for (int i = 0; i < count; i++) {
        __fini_array_start[i] ();
    }
}

static inline int is_nan_f(float x) {
    union { float f; uint32_t u; } v = { x };
    return ((v.u & 0x7f800000u) == 0x7f800000u) && (v.u & 0x007fffffu);
}

static inline int is_inf_pos_f(float x) {
    union { float f; uint32_t u; } v = { x };
    return v.u == 0x7f800000u; // +inf
}

static inline float qnan_f(void) {
    union { uint32_t u; float f; } v = { 0x7fc00000u }; // quiet NaN
    return v.f;
}

float sqrtf_(float x)
{
    // Спеціальні випадки згідно з IEEE-754 (спрощено)
    if (x == 0.0f)           return 0.0f;   // ±0 -> 0
    if (is_inf_pos_f(x))     return x;      // +inf -> +inf
    if (is_nan_f(x))         return x;      // NaN -> NaN
    if (x < 0.0f)            return qnan_f(); // від’ємні -> NaN

    // Швидка початкова оцінка через fast inverse sqrt (Quake trick)
    float xhalf = 0.5f * x;
    union { float f; uint32_t i; } y = { x };
    y.i = 0x5f3759dfu - (y.i >> 1);  // початкове наближення 1/sqrt(x)

    // Два кроки Ньютона для 1/sqrt(x)
    y.f = y.f * (1.5f - xhalf * y.f * y.f);
    y.f = y.f * (1.5f - xhalf * y.f * y.f);

    // sqrt(x) = x * (1/sqrt(x))
    return x * y.f;
}

/**
  * @brief Initialize the minimal C/C++ runtime for the embedded system.
  *
  * This function performs the essential startup tasks normally handled
  * by the C runtime before calling main():
  * - Copies the initialized data section (.data) from Flash to RAM.
  * - Clears the uninitialized data section (.bss) to zero.
  * - Initializes the syscall dispatch table used by libc.
  * - Disables buffering for @c stdout so that output is written immediately.
  * - Invokes constructors of global/static C++ objects via @c __libc_init_array().
  *
  * @param[in] syscalls Pointer to a populated libc_syscalls_t structure
  *                     that provides the system call implementations
  *                     (I/O, memory, time, etc).
  *
  * @note This function must be called once at system startup before
  *       using any standard C or C++ library facilities.
  */
void LibcIM_init(const struct libc_syscalls_t* syscalls)
{
    uint32_t *pSrc, *pDest;

    /////////////////////////////////////////////
    //// Copy .data from Flash to RAM
    /////////////////////////////////////////////
    pSrc = &_sidata;
    for (pDest = &_sdata; pDest < &_edata; ) {
        *pDest++ = *pSrc++;
    }

    /////////////////////////////////////////////
    //// Zero initialize the .bss section
    /////////////////////////////////////////////
    for (pDest = &_sbss; pDest < &_ebss; ) {
        *pDest++ = 0;
    }

    /////////////////////////////////////////////
    //// Set system callbacks
    /////////////////////////////////////////////

    LibcIM_syscallsInit(syscalls);

    /////////////////////////////////////////////
    //// Call constructors of static C++ objects
    /////////////////////////////////////////////

    __libc_init_array();

    /////////////////////////////////////////////
    //// Disable stdout buffering
    /////////////////////////////////////////////

    setbuf(stdout, NULL);
}

#ifdef __cplusplus
  }
#endif
