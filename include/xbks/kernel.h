#ifndef XBKS_KERNEL_H
#define XBKS_KERNEL_H

#include <xbks/boot_info.h>
#include <xbks/compiler.h>

XBKS_NORETURN void xbks_kernel_main(const struct xbks_boot_info *boot_info);

#endif
