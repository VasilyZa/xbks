#ifndef XBKS_BOOT_INFO_H
#define XBKS_BOOT_INFO_H

#include <limine.h>
#include <xbks/types.h>

struct xbks_boot_info {
    const struct limine_bootloader_info_response *bootloader;
    const struct limine_executable_cmdline_response *cmdline;
    const struct limine_firmware_type_response *firmware;
    const struct limine_hhdm_response *hhdm;
    const struct limine_memmap_response *memmap;
    const struct limine_executable_file_response *executable_file;
    const struct limine_framebuffer_response *framebuffer;
    const struct limine_module_response *modules;
};

#endif
