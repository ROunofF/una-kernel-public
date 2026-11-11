/**
 ******************************************************************************
 * @file    main.сpp
 * @brief   Main program body.
 ******************************************************************************
 *
 ******************************************************************************
 */
#include "Porting.h"

#include "Libc/KernelSyscalls.h"
#include "Time/TimePorting.hpp"

extern void (*__preinit_array_start []) (void) __attribute__((weak));
extern void (*__preinit_array_end [])   (void) __attribute__((weak));
extern void (*__init_array_start [])    (void) __attribute__((weak));
extern void (*__init_array_end [])      (void) __attribute__((weak));
extern void (*__fini_array_start[])     (void) __attribute__((weak));
extern void (*__fini_array_end[])       (void) __attribute__((weak));

void* __dso_handle = nullptr;

extern "C" {
    /* Iterate over all the init routines.  */
    void __una_init_array (void)
    {
        size_t count;
        size_t i;

        count = __preinit_array_end - __preinit_array_start;
        for (i = 0; i < count; ++i) {
            __preinit_array_start[i] ();
        }

        #ifdef _HAVE_INIT_FINI
            _init ();
        #endif

        count = __init_array_end - __init_array_start;
        for (i = 0; i < count; ++i) {
            __init_array_start[i] ();
        }
    }

    void __una_fini_array(void)
    {
        int count = __fini_array_end - __fini_array_start;
        for (int i = 0; i < count; i++) {
            __fini_array_start[i] ();
        }
    }

    void __init_libc(void)
    {
        static struct libc_syscalls_t syscalls;

        KernelSyscalls_assignProcPort(&syscalls.proc);
        KernelSyscalls_assignFilePort(&syscalls.file);
        KernelSyscalls_assignMemPort(&syscalls.mem);

        TimePorting_assignTime(&syscalls.time);

        LibcIM_init(&syscalls);

        __una_init_array();
    }
}
