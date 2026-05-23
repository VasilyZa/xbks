#include <xbks/elf.h>
#include <xbks/keyboard.h>
#include <xbks/log.h>
#include <xbks/ramdisk.h>
#include <xbks/string.h>
#include <xbks/uapi/errno.h>
#include <xbks/user_api.h>

typedef int (*xbks_user_entry_t)(
    const struct xbks_user_api *api,
    int argc,
    char **argv,
    char **envp
);

extern uint8_t __xsh_exec_base[];
extern uint8_t __xsh_exec_end[];
extern uint8_t __cmd_exec_base[];
extern uint8_t __cmd_exec_end[];

static bool command_running;

static int64_t user_api_read(int fd, void *buffer, uint64_t count);
static int64_t user_api_write(int fd, const void *buffer, uint64_t count);
static int64_t user_api_exec(const char *path, int argc, char **argv, char **envp);
static int64_t user_api_exists(const char *path);
static int64_t user_api_file_size(const char *path);
static int64_t user_api_read_file(const char *path, uint64_t offset, void *buffer, uint64_t count);
static int64_t user_api_read_dir(const char *path, uint64_t index, struct xbks_user_dirent *out);

static int64_t negative_errno(int error) {
    return -(int64_t)error;
}

static bool user_buffer_is_valid(const void *buffer, uint64_t count) {
    if (count == 0) {
        return true;
    }

    return buffer != 0;
}

static int64_t user_api_read(int fd, void *buffer, uint64_t count) {
    char *out = buffer;

    if (fd != 0) {
        return negative_errno(XBKS_EBADF);
    }

    if (!user_buffer_is_valid(buffer, count)) {
        return negative_errno(XBKS_EFAULT);
    }

    for (uint64_t i = 0; i < count; ++i) {
        out[i] = xbks_keyboard_read_char_blocking();
    }

    return (int64_t)count;
}

static int64_t user_api_write(int fd, const void *buffer, uint64_t count) {
    if (fd != 1 && fd != 2) {
        return negative_errno(XBKS_EBADF);
    }

    if (!user_buffer_is_valid(buffer, count)) {
        return negative_errno(XBKS_EFAULT);
    }

    xbks_console_write(buffer, (size_t)count);
    return (int64_t)count;
}

static int64_t user_api_exists(const char *path) {
    struct xbks_ramdisk_file file;

    if (path == 0) {
        return negative_errno(XBKS_EFAULT);
    }

    return xbks_ramdisk_find(path, &file) ? 1 : 0;
}

static int64_t user_api_file_size(const char *path) {
    struct xbks_ramdisk_file file;

    if (path == 0) {
        return negative_errno(XBKS_EFAULT);
    }

    if (!xbks_ramdisk_find(path, &file)) {
        return negative_errno(XBKS_ENOENT);
    }

    if (file.directory) {
        return negative_errno(XBKS_EISDIR);
    }

    return (int64_t)file.size;
}

static int64_t user_api_read_file(const char *path, uint64_t offset, void *buffer, uint64_t count) {
    struct xbks_ramdisk_file file;

    if (path == 0 || !user_buffer_is_valid(buffer, count)) {
        return negative_errno(XBKS_EFAULT);
    }

    if (!xbks_ramdisk_find(path, &file)) {
        return negative_errno(XBKS_ENOENT);
    }

    if (file.directory) {
        return negative_errno(XBKS_EISDIR);
    }

    if (offset >= file.size) {
        return 0;
    }

    size_t available = file.size - (size_t)offset;
    if (count < available) {
        available = (size_t)count;
    }

    memcpy(buffer, (const uint8_t *)file.data + offset, available);
    return (int64_t)available;
}

static const char *basename(const char *path) {
    const char *base = path;

    for (const char *cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            base = cursor + 1;
        }
    }

    return base;
}

static void copy_string_limited(char *dest, size_t capacity, const char *src) {
    size_t i = 0;

    if (capacity == 0) {
        return;
    }

    while (i + 1u < capacity && src[i] != '\0') {
        dest[i] = src[i];
        ++i;
    }

    dest[i] = '\0';
}

static int64_t user_api_read_dir(const char *path, uint64_t index, struct xbks_user_dirent *out) {
    struct xbks_ramdisk_file file;

    if (path == 0 || out == 0) {
        return negative_errno(XBKS_EFAULT);
    }

    if (!xbks_ramdisk_read_dir(path, (size_t)index, &file)) {
        return negative_errno(XBKS_ENOENT);
    }

    copy_string_limited(out->name, sizeof(out->name), basename(file.path));
    out->size = file.size;
    out->type = file.directory ? XBKS_USER_DIRENT_DIR : XBKS_USER_DIRENT_FILE;
    out->executable = file.executable ? 1u : 0u;
    return 0;
}

static int64_t run_loaded_image(
    const struct xbks_ramdisk_file *file,
    uintptr_t arena_base,
    size_t arena_size,
    int argc,
    char **argv,
    char **envp
) {
    struct xbks_elf_image image;

    if (file->directory) {
        return negative_errno(XBKS_EISDIR);
    }

    if (!file->executable) {
        return negative_errno(XBKS_EACCES);
    }

    if (!xbks_elf_load_image(file, arena_base, arena_size, &image)) {
        return negative_errno(XBKS_ENOEXEC);
    }

    xbks_user_entry_t entry = (xbks_user_entry_t)(uintptr_t)image.entry;
    return (int64_t)entry(&((const struct xbks_user_api) {
        .version = XBKS_USER_API_VERSION,
        .read = user_api_read,
        .write = user_api_write,
        .exec = user_api_exec,
        .exists = user_api_exists,
        .file_size = user_api_file_size,
        .read_file = user_api_read_file,
        .read_dir = user_api_read_dir,
    }), argc, argv, envp);
}

static int64_t user_api_exec(const char *path, int argc, char **argv, char **envp) {
    struct xbks_ramdisk_file file;
    const uintptr_t arena_base = (uintptr_t)__cmd_exec_base;
    const size_t arena_size = (size_t)(__cmd_exec_end - __cmd_exec_base);

    if (path == 0 || argc < 0 || argv == 0) {
        return negative_errno(XBKS_EFAULT);
    }

    if (command_running) {
        return negative_errno(XBKS_EBUSY);
    }

    if (!xbks_ramdisk_find(path, &file)) {
        return negative_errno(XBKS_ENOENT);
    }

    command_running = true;
    const int64_t status = run_loaded_image(&file, arena_base, arena_size, argc, argv, envp);
    command_running = false;
    return status;
}

static const struct xbks_user_api early_user_api = {
    .version = XBKS_USER_API_VERSION,
    .read = user_api_read,
    .write = user_api_write,
    .exec = user_api_exec,
    .exists = user_api_exists,
    .file_size = user_api_file_size,
    .read_file = user_api_read_file,
    .read_dir = user_api_read_dir,
};

bool xbks_userland_start(const struct xbks_boot_info *boot_info) {
    struct xbks_ramdisk_file xsh_file;
    struct xbks_elf_image image;
    char *argv[] = { (char *)"xsh", 0 };
    char *envp[] = { (char *)"PATH=/bin:/sbin:/usr/bin", 0 };
    const uintptr_t arena_base = (uintptr_t)__xsh_exec_base;
    const size_t arena_size = (size_t)(__xsh_exec_end - __xsh_exec_base);

    if (boot_info == 0) {
        return false;
    }

    if (!xbks_ramdisk_init(boot_info->modules)) {
        xbks_log_write(XBKS_LOG_WARN, "no RAMDISK module found; falling back to kernel shell");
        return false;
    }

    if (!xbks_ramdisk_find("/bin/xsh", &xsh_file)) {
        xbks_log_write(XBKS_LOG_WARN, "RAMDISK has no /bin/xsh; falling back to kernel shell");
        return false;
    }

    if (!xbks_elf_load_image(&xsh_file, arena_base, arena_size, &image)) {
        xbks_log_write(XBKS_LOG_ERROR, "failed to load /bin/xsh as ELF64");
        return false;
    }

    xbks_keyboard_init();
    xbks_log_write(XBKS_LOG_INFO, "starting XiaobaiOS user shell /bin/xsh");

    xbks_user_entry_t entry = (xbks_user_entry_t)(uintptr_t)image.entry;
    const int status = entry(&early_user_api, 1, argv, envp);
    xbks_log_printf(XBKS_LOG_INFO, "/bin/xsh returned status %d", status);
    return true;
}
