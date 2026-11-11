#ifndef __LIBC_IMAGE_MAKER_H__
#define __LIBC_IMAGE_MAKER_H__

#include <stdint.h>
#include <time.h>

/* Typedefs for system call functions */
typedef int   (*getpid_t)(void);
typedef int   (*kill_t)(int pid, int sig);
typedef void  (*exit_t)(int status);
typedef int   (*read_t)(int file, char *ptr, int len);
typedef int   (*write_t)(int file, char *ptr, int len);
typedef int   (*close_t)(int file);
typedef int   (*fstat_t)(int file, struct stat *st);
typedef int   (*isatty_t)(int file);
typedef int   (*lseek_t)(int file, int ptr, int dir);
typedef int   (*open_t)(char *path, int flags, ...);
typedef int   (*wait_t)(int *status);
typedef int   (*unlink_t)(char *name);
typedef int   (*times_t)(struct tms *buf);
typedef int   (*stat_t)(char *file, struct stat *st);
typedef int   (*link_t)(char *old, char *new_);
typedef int   (*fork_t)(void);
typedef int   (*execve_t)(char *name, char **argv, char **env);
typedef void* (*malloc_t)(uint32_t size);
typedef void  (*free_t)(void* buff);
typedef int   (*gettimeofday_t)(struct timeval *tv);

// ---- Grouped sub-structures ----

/** Process/posix-ish syscalls */
struct libc_syscalls_proc_t {
    getpid_t getpid;
    kill_t   kill;
    exit_t   exit_;
    wait_t   wait;
    fork_t   fork;
    execve_t execve;
    times_t  getTimes;
};

/** File/IO syscalls */
struct libc_syscalls_file_t {
    read_t   read;
    write_t  write;
    close_t  close;
    fstat_t  fstat;
    isatty_t isatty;
    lseek_t  lseek;
    open_t   open;
    unlink_t unlink;
    stat_t   stat;
    link_t   link;
};

/** Memory allocators */
struct libc_syscalls_mem_t {
    malloc_t malloc_;
    free_t   free_;
};

/** Time-related syscalls */
struct libc_syscalls_time_t {
    gettimeofday_t getTime;
};

/** Master table composed of groups */
struct libc_syscalls_t {
    struct libc_syscalls_proc_t proc;
    struct libc_syscalls_file_t file;
    struct libc_syscalls_mem_t  mem;
    struct libc_syscalls_time_t time;
};

#ifdef __cplusplus
  extern "C" {
#endif

void LibcIM_syscallsInit(const struct libc_syscalls_t* syscalls);
void LibcIM_init(const struct  libc_syscalls_t* syscalls);

#ifdef __cplusplus
  }
#endif

#endif
