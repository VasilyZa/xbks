#ifndef XBKS_ELF_H
#define XBKS_ELF_H

#include <xbks/ramdisk.h>
#include <xbks/types.h>

struct xbks_elf_image {
    uintptr_t entry;
    uintptr_t base;
    size_t size;
};

bool xbks_elf_load_image(
    const struct xbks_ramdisk_file *file,
    uintptr_t arena_base,
    size_t arena_size,
    struct xbks_elf_image *out
);

#endif
