#ifndef XBKS_PROCESS_H
#define XBKS_PROCESS_H

#include <xbks/boot_info.h>
#include <xbks/syscall.h>
#include <xbks/types.h>

#define XBKS_USER_TOP 0x0000800000000000ull
#define XBKS_USER_STACK_TOP 0x00007fffffffe000ull
#define XBKS_USER_STACK_SIZE (16ull * 4096ull)

bool xbks_process_start_init(const struct xbks_boot_info *boot_info);
uint64_t xbks_process_syscall_dispatch(struct xbks_syscall_frame *frame);

#endif
