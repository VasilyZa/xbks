#include <limine.h>
#include <xbks/compiler.h>
#include <xbks/log.h>
#include <xbks/ramdisk.h>
#include <xbks/string.h>

enum {
    TAR_BLOCK_SIZE = 512,
    TAR_NAME_SIZE = 100,
    TAR_PREFIX_SIZE = 155,
    RAMDISK_PATH_SIZE = sizeof(((struct xbks_ramdisk_file *)0)->path),
};

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} XBKS_PACKED;

static const uint8_t *ramdisk_data;
static size_t ramdisk_size;
static bool ramdisk_available;

static size_t bounded_strlen(const char *text, size_t limit) {
    size_t length = 0;

    while (length < limit && text[length] != '\0') {
        ++length;
    }

    return length;
}

static bool block_is_zero(const void *data) {
    const uint8_t *bytes = data;

    for (size_t i = 0; i < TAR_BLOCK_SIZE; ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }

    return true;
}

static uint64_t parse_octal(const char *text, size_t limit) {
    uint64_t value = 0;
    size_t i = 0;

    while (i < limit && (text[i] == ' ' || text[i] == '\0')) {
        ++i;
    }

    for (; i < limit; ++i) {
        if (text[i] < '0' || text[i] > '7') {
            break;
        }

        value = (value << 3) + (uint64_t)(text[i] - '0');
    }

    return value;
}

static size_t align_to_tar_block(size_t value) {
    return (value + TAR_BLOCK_SIZE - 1u) & ~(size_t)(TAR_BLOCK_SIZE - 1u);
}

static bool string_ends_with(const char *text, const char *suffix) {
    const size_t text_length = strlen(text);
    const size_t suffix_length = strlen(suffix);

    if (suffix_length > text_length) {
        return false;
    }

    return strcmp(text + text_length - suffix_length, suffix) == 0;
}

static void copy_bytes(char *dest, size_t capacity, const char *src, size_t length) {
    size_t count = length;

    if (capacity == 0) {
        return;
    }

    if (count >= capacity) {
        count = capacity - 1u;
    }

    for (size_t i = 0; i < count; ++i) {
        dest[i] = src[i];
    }

    dest[count] = '\0';
}

static void normalize_path(const char *path, char *out, size_t capacity) {
    size_t output_length = 0;

    if (capacity == 0) {
        return;
    }

    while (*path == '/') {
        ++path;
    }

    while (path[0] == '.' && (path[1] == '/' || path[1] == '\0')) {
        ++path;
        while (*path == '/') {
            ++path;
        }
    }

    while (*path != '\0' && output_length + 1u < capacity) {
        out[output_length] = *path;
        ++output_length;
        ++path;
    }

    while (output_length > 0 && out[output_length - 1u] == '/') {
        --output_length;
    }

    out[output_length] = '\0';
}

static void make_public_path(const char *normalized, char *out, size_t capacity) {
    if (capacity == 0) {
        return;
    }

    if (normalized[0] == '\0') {
        copy_bytes(out, capacity, "/", 1);
        return;
    }

    out[0] = '/';
    copy_bytes(out + 1, capacity - 1u, normalized, strlen(normalized));
}

static bool tar_path(const struct tar_header *header, char *out, size_t capacity) {
    char combined[RAMDISK_PATH_SIZE];
    const size_t prefix_length = bounded_strlen(header->prefix, TAR_PREFIX_SIZE);
    const size_t name_length = bounded_strlen(header->name, TAR_NAME_SIZE);
    size_t offset = 0;

    if (name_length == 0) {
        return false;
    }

    if (prefix_length != 0) {
        copy_bytes(combined, sizeof(combined), header->prefix, prefix_length);
        offset = strlen(combined);
        if (offset + 1u < sizeof(combined)) {
            combined[offset] = '/';
            ++offset;
            combined[offset] = '\0';
        }
    } else {
        combined[0] = '\0';
    }

    copy_bytes(combined + offset, sizeof(combined) - offset, header->name, name_length);
    normalize_path(combined, out, capacity);
    return out[0] != '\0';
}

static bool header_is_directory(const struct tar_header *header, const char *normalized_path) {
    const size_t length = strlen(normalized_path);

    if (header->typeflag == '5') {
        return true;
    }

    return length != 0 && normalized_path[length - 1u] == '/';
}

static void fill_file(
    const struct tar_header *header,
    const char *normalized_path,
    const void *data,
    size_t size,
    struct xbks_ramdisk_file *out
) {
    const uint64_t mode = parse_octal(header->mode, sizeof(header->mode));

    make_public_path(normalized_path, out->path, sizeof(out->path));
    out->data = data;
    out->size = size;
    out->directory = header_is_directory(header, normalized_path);
    out->executable = (mode & 0111u) != 0;
}

static bool find_header(
    const char *normalized_path,
    struct xbks_ramdisk_file *out,
    bool *out_is_directory_prefix
) {
    size_t offset = 0;
    bool directory_prefix = false;

    while (offset + TAR_BLOCK_SIZE <= ramdisk_size) {
        const struct tar_header *header = (const struct tar_header *)(const void *)(ramdisk_data + offset);

        if (block_is_zero(header)) {
            break;
        }

        const size_t file_size = (size_t)parse_octal(header->size, sizeof(header->size));
        const size_t data_offset = offset + TAR_BLOCK_SIZE;
        char path[RAMDISK_PATH_SIZE];

        if (data_offset > ramdisk_size || file_size > ramdisk_size - data_offset) {
            break;
        }

        if (tar_path(header, path, sizeof(path))) {
            if (strcmp(path, normalized_path) == 0) {
                if (out != 0) {
                    fill_file(header, path, ramdisk_data + data_offset, file_size, out);
                }
                if (out_is_directory_prefix != 0) {
                    *out_is_directory_prefix = false;
                }
                return true;
            }

            const size_t query_length = strlen(normalized_path);
            if (query_length != 0 &&
                strncmp(path, normalized_path, query_length) == 0 &&
                path[query_length] == '/') {
                directory_prefix = true;
            }
        }

        offset = data_offset + align_to_tar_block(file_size);
    }

    if (out_is_directory_prefix != 0) {
        *out_is_directory_prefix = directory_prefix;
    }

    return false;
}

static bool child_name_for_directory(
    const char *entry_path,
    const char *directory_path,
    char *child_name,
    size_t child_name_capacity,
    bool *out_nested
) {
    const char *relative = entry_path;
    const size_t directory_length = strlen(directory_path);
    size_t child_length = 0;

    if (directory_length != 0) {
        if (strncmp(entry_path, directory_path, directory_length) != 0 ||
            entry_path[directory_length] != '/') {
            return false;
        }

        relative = entry_path + directory_length + 1u;
    }

    if (*relative == '\0') {
        return false;
    }

    while (relative[child_length] != '\0' && relative[child_length] != '/') {
        ++child_length;
    }

    if (child_length == 0) {
        return false;
    }

    copy_bytes(child_name, child_name_capacity, relative, child_length);
    *out_nested = relative[child_length] == '/';
    return true;
}

static bool child_seen_before(const char *directory_path, const char *child_name, size_t current_offset) {
    size_t offset = 0;

    while (offset < current_offset && offset + TAR_BLOCK_SIZE <= ramdisk_size) {
        const struct tar_header *header = (const struct tar_header *)(const void *)(ramdisk_data + offset);

        if (block_is_zero(header)) {
            return false;
        }

        const size_t file_size = (size_t)parse_octal(header->size, sizeof(header->size));
        const size_t data_offset = offset + TAR_BLOCK_SIZE;
        char path[RAMDISK_PATH_SIZE];
        char previous_child[RAMDISK_PATH_SIZE];
        bool nested = false;

        if (data_offset > ramdisk_size || file_size > ramdisk_size - data_offset) {
            return false;
        }

        if (tar_path(header, path, sizeof(path)) &&
            child_name_for_directory(path, directory_path, previous_child, sizeof(previous_child), &nested) &&
            strcmp(previous_child, child_name) == 0) {
            return true;
        }

        offset = data_offset + align_to_tar_block(file_size);
    }

    return false;
}

bool xbks_ramdisk_init(const struct limine_module_response *modules) {
    ramdisk_data = 0;
    ramdisk_size = 0;
    ramdisk_available = false;

    if (modules == 0 || modules->module_count == 0 || modules->modules == 0) {
        return false;
    }

    for (uint64_t i = 0; i < modules->module_count; ++i) {
        const struct limine_file *module = modules->modules[i];

        if (module == 0 || module->address == 0 || module->size == 0) {
            continue;
        }

        const char *module_string = module->string != 0 ? module->string : "";
        const char *module_path = module->path != 0 ? module->path : "";

        if (strcmp(module_string, "ramdisk") == 0 ||
            string_ends_with(module_path, "/ramdisk.tar") ||
            string_ends_with(module_path, "ramdisk.tar")) {
            ramdisk_data = module->address;
            ramdisk_size = (size_t)module->size;
            ramdisk_available = true;
            xbks_log_printf(XBKS_LOG_INFO, "RAMDISK loaded: %llu bytes", module->size);
            return true;
        }
    }

    if (modules->module_count == 1) {
        const struct limine_file *module = modules->modules[0];

        if (module != 0 && module->address != 0 && module->size != 0) {
            ramdisk_data = module->address;
            ramdisk_size = (size_t)module->size;
            ramdisk_available = true;
            xbks_log_printf(XBKS_LOG_INFO, "RAMDISK loaded from only module: %llu bytes", module->size);
            return true;
        }
    }

    return false;
}

bool xbks_ramdisk_is_available(void) {
    return ramdisk_available;
}

bool xbks_ramdisk_find(const char *path, struct xbks_ramdisk_file *out) {
    char normalized[RAMDISK_PATH_SIZE];
    bool directory_prefix = false;

    if (!ramdisk_available || path == 0 || out == 0) {
        return false;
    }

    normalize_path(path, normalized, sizeof(normalized));
    if (normalized[0] == '\0') {
        make_public_path(normalized, out->path, sizeof(out->path));
        out->data = 0;
        out->size = 0;
        out->directory = true;
        out->executable = false;
        return true;
    }

    if (find_header(normalized, out, &directory_prefix)) {
        return true;
    }

    if (directory_prefix) {
        make_public_path(normalized, out->path, sizeof(out->path));
        out->data = 0;
        out->size = 0;
        out->directory = true;
        out->executable = false;
        return true;
    }

    return false;
}

bool xbks_ramdisk_read_dir(const char *path, size_t index, struct xbks_ramdisk_file *out) {
    char normalized[RAMDISK_PATH_SIZE];
    size_t offset = 0;
    size_t current_index = 0;

    if (!ramdisk_available || path == 0 || out == 0) {
        return false;
    }

    normalize_path(path, normalized, sizeof(normalized));

    while (offset + TAR_BLOCK_SIZE <= ramdisk_size) {
        const struct tar_header *header = (const struct tar_header *)(const void *)(ramdisk_data + offset);

        if (block_is_zero(header)) {
            break;
        }

        const size_t file_size = (size_t)parse_octal(header->size, sizeof(header->size));
        const size_t data_offset = offset + TAR_BLOCK_SIZE;
        char entry_path[RAMDISK_PATH_SIZE];
        char child_name[RAMDISK_PATH_SIZE];
        bool nested = false;

        if (data_offset > ramdisk_size || file_size > ramdisk_size - data_offset) {
            break;
        }

        if (tar_path(header, entry_path, sizeof(entry_path)) &&
            child_name_for_directory(entry_path, normalized, child_name, sizeof(child_name), &nested) &&
            !child_seen_before(normalized, child_name, offset)) {
            if (current_index == index) {
                char child_path[RAMDISK_PATH_SIZE];

                if (normalized[0] == '\0') {
                    copy_bytes(child_path, sizeof(child_path), child_name, strlen(child_name));
                } else {
                    copy_bytes(child_path, sizeof(child_path), normalized, strlen(normalized));
                    const size_t length = strlen(child_path);
                    if (length + 1u < sizeof(child_path)) {
                        child_path[length] = '/';
                        child_path[length + 1u] = '\0';
                    }
                    copy_bytes(child_path + strlen(child_path),
                               sizeof(child_path) - strlen(child_path),
                               child_name,
                               strlen(child_name));
                }

                make_public_path(child_path, out->path, sizeof(out->path));
                out->data = nested ? 0 : ramdisk_data + data_offset;
                out->size = nested ? 0 : file_size;
                out->directory = nested || header_is_directory(header, entry_path);
                out->executable = !out->directory &&
                    ((parse_octal(header->mode, sizeof(header->mode)) & 0111u) != 0);
                return true;
            }

            ++current_index;
        }

        offset = data_offset + align_to_tar_block(file_size);
    }

    return false;
}
