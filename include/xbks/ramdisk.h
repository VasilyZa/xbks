#ifndef XBKS_RAMDISK_H
#define XBKS_RAMDISK_H

#include <limine.h>
#include <xbks/types.h>

struct xbks_ramdisk_file {
    char path[256];
    const void *data;
    size_t size;
    bool directory;
    bool executable;
};

bool xbks_ramdisk_init(const struct limine_module_response *modules);
bool xbks_ramdisk_is_available(void);
bool xbks_ramdisk_find(const char *path, struct xbks_ramdisk_file *out);
bool xbks_ramdisk_read_dir(const char *path, size_t index, struct xbks_ramdisk_file *out);

#endif
