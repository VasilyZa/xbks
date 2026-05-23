#ifndef XBKS_USER_API_H
#define XBKS_USER_API_H

#include <xbks/boot_info.h>
#include <xbks/types.h>

#define XBKS_USER_API_VERSION 1u

struct xbks_user_dirent {
    char name[128];
    uint64_t size;
    uint32_t type;
    uint32_t executable;
};

enum {
    XBKS_USER_DIRENT_FILE = 1,
    XBKS_USER_DIRENT_DIR = 2,
};

struct xbks_user_api {
    uint64_t version;
    int64_t (*read)(int fd, void *buffer, uint64_t count);
    int64_t (*write)(int fd, const void *buffer, uint64_t count);
    int64_t (*exec)(const char *path, int argc, char **argv, char **envp);
    int64_t (*exists)(const char *path);
    int64_t (*file_size)(const char *path);
    int64_t (*read_file)(const char *path, uint64_t offset, void *buffer, uint64_t count);
    int64_t (*read_dir)(const char *path, uint64_t index, struct xbks_user_dirent *out);
};

bool xbks_userland_start(const struct xbks_boot_info *boot_info);

#endif
