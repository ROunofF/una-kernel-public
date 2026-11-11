#ifndef __KERNEL_SYSCALL_H__
#define __KERNEL_SYSCALL_H__

#include "Libc/ImageMaker.h"

#ifdef __cplusplus
  extern "C" {
#endif

void KernelSyscalls_assignProcPort(struct libc_syscalls_proc_t* port);
void KernelSyscalls_assignFilePort(struct libc_syscalls_file_t* port);
void KernelSyscalls_assignMemPort(struct libc_syscalls_mem_t* port);

#ifdef __cplusplus
  }
#endif

#endif
