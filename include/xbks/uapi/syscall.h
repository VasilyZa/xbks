#ifndef XBKS_UAPI_SYSCALL_H
#define XBKS_UAPI_SYSCALL_H

/*
 * xbks follows the Linux x86_64 syscall register ABI:
 *   rax = syscall number
 *   rdi, rsi, rdx, r10, r8, r9 = arguments 0..5
 *   rax = non-negative result or negative errno
 *
 * The numbers below intentionally match Linux x86_64 for the common calls so
 * a small libc port can start with minimal architecture glue.
 */
#define XBKS_SYS_READ        0
#define XBKS_SYS_WRITE       1
#define XBKS_SYS_OPEN        2
#define XBKS_SYS_CLOSE       3
#define XBKS_SYS_STAT        4
#define XBKS_SYS_FSTAT       5
#define XBKS_SYS_LSTAT       6
#define XBKS_SYS_POLL        7
#define XBKS_SYS_LSEEK       8
#define XBKS_SYS_MMAP        9
#define XBKS_SYS_MPROTECT    10
#define XBKS_SYS_MUNMAP      11
#define XBKS_SYS_BRK         12
#define XBKS_SYS_RT_SIGACTION 13
#define XBKS_SYS_RT_SIGPROCMASK 14
#define XBKS_SYS_IOCTL       16
#define XBKS_SYS_PREAD64     17
#define XBKS_SYS_PWRITE64    18
#define XBKS_SYS_READV       19
#define XBKS_SYS_WRITEV      20
#define XBKS_SYS_ACCESS      21
#define XBKS_SYS_PIPE        22
#define XBKS_SYS_SELECT      23
#define XBKS_SYS_SCHED_YIELD 24
#define XBKS_SYS_MREMAP      25
#define XBKS_SYS_MSYNC       26
#define XBKS_SYS_MINCORE     27
#define XBKS_SYS_MADVISE     28
#define XBKS_SYS_SHMGET      29
#define XBKS_SYS_DUP         32
#define XBKS_SYS_DUP2        33
#define XBKS_SYS_NANOSLEEP   35
#define XBKS_SYS_GETPID      39
#define XBKS_SYS_SOCKET      41
#define XBKS_SYS_CONNECT     42
#define XBKS_SYS_ACCEPT      43
#define XBKS_SYS_SENDTO      44
#define XBKS_SYS_RECVFROM    45
#define XBKS_SYS_BIND        49
#define XBKS_SYS_LISTEN      50
#define XBKS_SYS_CLONE       56
#define XBKS_SYS_FORK        57
#define XBKS_SYS_VFORK       58
#define XBKS_SYS_EXECVE      59
#define XBKS_SYS_EXIT        60
#define XBKS_SYS_WAIT4       61
#define XBKS_SYS_KILL        62
#define XBKS_SYS_UNAME       63
#define XBKS_SYS_FCNTL       72
#define XBKS_SYS_FSYNC       74
#define XBKS_SYS_FTRUNCATE   77
#define XBKS_SYS_GETCWD      79
#define XBKS_SYS_CHDIR       80
#define XBKS_SYS_MKDIR       83
#define XBKS_SYS_RMDIR       84
#define XBKS_SYS_CREAT       85
#define XBKS_SYS_LINK        86
#define XBKS_SYS_UNLINK      87
#define XBKS_SYS_READLINK    89
#define XBKS_SYS_CHMOD       90
#define XBKS_SYS_GETTIMEOFDAY 96
#define XBKS_SYS_GETUID      102
#define XBKS_SYS_GETGID      104
#define XBKS_SYS_GETEUID     107
#define XBKS_SYS_GETEGID     108
#define XBKS_SYS_GETPPID     110
#define XBKS_SYS_GETPGID     121
#define XBKS_SYS_ARCH_PRCTL  158
#define XBKS_SYS_GETTID      186
#define XBKS_SYS_TIME        201
#define XBKS_SYS_FUTEX       202
#define XBKS_SYS_GETDENTS64  217
#define XBKS_SYS_SET_TID_ADDRESS 218
#define XBKS_SYS_CLOCK_GETTIME 228
#define XBKS_SYS_EXIT_GROUP  231
#define XBKS_SYS_OPENAT      257
#define XBKS_SYS_MKDIRAT     258
#define XBKS_SYS_UNLINKAT    263
#define XBKS_SYS_READLINKAT  267
#define XBKS_SYS_FACCESSAT   269

#ifndef __ASSEMBLER__
#include <stdint.h>

static inline long xbks_syscall0(long number) {
    long result;
    __asm__ volatile (
        "syscall"
        : "=a" (result)
        : "a" (number)
        : "rcx", "r11", "memory"
    );
    return result;
}

static inline long xbks_syscall1(long number, long arg0) {
    long result;
    __asm__ volatile (
        "syscall"
        : "=a" (result)
        : "a" (number), "D" (arg0)
        : "rcx", "r11", "memory"
    );
    return result;
}

static inline long xbks_syscall2(long number, long arg0, long arg1) {
    long result;
    __asm__ volatile (
        "syscall"
        : "=a" (result)
        : "a" (number), "D" (arg0), "S" (arg1)
        : "rcx", "r11", "memory"
    );
    return result;
}

static inline long xbks_syscall3(long number, long arg0, long arg1, long arg2) {
    long result;
    __asm__ volatile (
        "syscall"
        : "=a" (result)
        : "a" (number), "D" (arg0), "S" (arg1), "d" (arg2)
        : "rcx", "r11", "memory"
    );
    return result;
}

static inline long xbks_syscall4(long number, long arg0, long arg1, long arg2, long arg3) {
    register long r10 __asm__("r10") = arg3;
    long result;
    __asm__ volatile (
        "syscall"
        : "=a" (result)
        : "a" (number), "D" (arg0), "S" (arg1), "d" (arg2), "r" (r10)
        : "rcx", "r11", "memory"
    );
    return result;
}

static inline long xbks_syscall5(long number, long arg0, long arg1, long arg2, long arg3, long arg4) {
    register long r10 __asm__("r10") = arg3;
    register long r8 __asm__("r8") = arg4;
    long result;
    __asm__ volatile (
        "syscall"
        : "=a" (result)
        : "a" (number), "D" (arg0), "S" (arg1), "d" (arg2), "r" (r10), "r" (r8)
        : "rcx", "r11", "memory"
    );
    return result;
}

static inline long xbks_syscall6(long number, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5) {
    register long r10 __asm__("r10") = arg3;
    register long r8 __asm__("r8") = arg4;
    register long r9 __asm__("r9") = arg5;
    long result;
    __asm__ volatile (
        "syscall"
        : "=a" (result)
        : "a" (number), "D" (arg0), "S" (arg1), "d" (arg2), "r" (r10), "r" (r8), "r" (r9)
        : "rcx", "r11", "memory"
    );
    return result;
}
#endif

#endif
