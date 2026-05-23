#include <xbks/elf.h>
#include <xbks/format.h>
#include <xbks/io.h>
#include <xbks/keyboard.h>
#include <xbks/log.h>
#include <xbks/mm.h>
#include <xbks/process.h>
#include <xbks/ramdisk.h>
#include <xbks/string.h>
#include <xbks/uapi/errno.h>
#include <xbks/uapi/syscall.h>

enum {
    MAX_PROCESSES = 16,
    MAX_FDS = 16,
    MAX_ARGS = 32,
    MAX_ENV = 32,
    MAX_STRING = 256,
    USER_IMAGE_MIN = 0x10000,
    PF_X = 1,
    PF_W = 2,
    PF_R = 4,
    AT_FDCWD = -100,
    DT_DIR = 4,
    DT_REG = 8,
};

enum process_state {
    PROCESS_UNUSED,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_ZOMBIE,
};

enum fd_type {
    FD_NONE,
    FD_STDIN,
    FD_STDOUT,
    FD_STDERR,
    FD_FILE,
    FD_DIR,
};

struct process_fd {
    enum fd_type type;
    struct xbks_ramdisk_file file;
    uint64_t offset;
};

struct process {
    int pid;
    int ppid;
    enum process_state state;
    uintptr_t cr3;
    uintptr_t entry;
    uintptr_t stack;
    uintptr_t brk;
    int exit_status;
    struct process *parent;
    struct process *vfork_parent;
    struct xbks_syscall_frame saved_parent_frame;
    struct process_fd fds[MAX_FDS];
};

struct elf64_ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[];
};

extern void xbks_x86_64_enter_user(uintptr_t entry, uintptr_t stack);

static struct process processes[MAX_PROCESSES];
static struct process *current_process;
static int next_pid = 1;

static uint64_t negative_errno(int error) {
    return (uint64_t)(-(int64_t)error);
}

static uintptr_t align_down(uintptr_t value, uintptr_t alignment) {
    return value & ~(alignment - 1u);
}

static uintptr_t align_up(uintptr_t value, uintptr_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static bool user_range_is_valid(uintptr_t address, size_t length) {
    if (length == 0) {
        return true;
    }

    if (address < USER_IMAGE_MIN || address >= XBKS_USER_TOP) {
        return false;
    }

    return length - 1u < XBKS_USER_TOP - address;
}

static bool copy_user_string(char *dest, size_t capacity, const char *user_string) {
    uintptr_t source = (uintptr_t)user_string;

    if (capacity == 0 || !user_range_is_valid(source, 1)) {
        return false;
    }

    for (size_t i = 0; i < capacity; ++i) {
        if (!user_range_is_valid(source + i, 1)) {
            return false;
        }

        dest[i] = user_string[i];
        if (dest[i] == '\0') {
            return true;
        }
    }

    dest[capacity - 1u] = '\0';
    return false;
}

static void copy_kernel_string(char *dest, size_t capacity, const char *src) {
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

static bool copy_user_string_array(
    char strings[][MAX_STRING],
    int max_strings,
    int *out_count,
    char *const user_values[]
) {
    int count = 0;

    if (user_values == 0) {
        *out_count = 0;
        return true;
    }

    while (count < max_strings) {
        if (!user_range_is_valid((uintptr_t)&user_values[count], sizeof(char *))) {
            return false;
        }

        char *value = user_values[count];
        if (value == 0) {
            *out_count = count;
            return true;
        }

        if (!copy_user_string(strings[count], MAX_STRING, value)) {
            return false;
        }

        ++count;
    }

    return false;
}

static struct process *allocate_process(void) {
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        if (processes[i].state == PROCESS_UNUSED) {
            memset(&processes[i], 0, sizeof(processes[i]));
            processes[i].pid = next_pid;
            ++next_pid;
            processes[i].state = PROCESS_RUNNING;
            processes[i].fds[0].type = FD_STDIN;
            processes[i].fds[1].type = FD_STDOUT;
            processes[i].fds[2].type = FD_STDERR;
            return &processes[i];
        }
    }

    return 0;
}

static int allocate_fd(struct process *process) {
    for (int i = 3; i < MAX_FDS; ++i) {
        if (process->fds[i].type == FD_NONE) {
            return i;
        }
    }

    return -1;
}

static bool map_user_page(uintptr_t cr3, uintptr_t virtual_address, uint64_t flags) {
    const uintptr_t frame = xbks_pmm_alloc_frame();

    if (frame == 0) {
        return false;
    }

    if (!xbks_vmm_map_page(cr3, virtual_address, frame, flags | XBKS_PAGE_USER)) {
        xbks_pmm_free_frame(frame);
        return false;
    }

    return true;
}

static bool map_user_range(uintptr_t cr3, uintptr_t base, size_t size, uint64_t flags) {
    const uintptr_t start = align_down(base, XBKS_PAGE_SIZE);
    const uintptr_t end = align_up(base + size, XBKS_PAGE_SIZE);

    for (uintptr_t page = start; page < end; page += XBKS_PAGE_SIZE) {
        if (!map_user_page(cr3, page, flags)) {
            return false;
        }
    }

    return true;
}

static bool write_process_memory(uintptr_t cr3, uintptr_t virtual_address, const void *src, size_t size) {
    const uint8_t *input = src;
    size_t written = 0;

    while (written < size) {
        uintptr_t physical = 0;
        const uintptr_t address = virtual_address + written;
        const size_t page_offset = address & (XBKS_PAGE_SIZE - 1u);
        size_t chunk = XBKS_PAGE_SIZE - page_offset;

        if (chunk > size - written) {
            chunk = size - written;
        }

        if (!xbks_vmm_translate(cr3, address, &physical)) {
            return false;
        }

        memcpy(xbks_phys_to_virt(physical), input + written, chunk);
        written += chunk;
    }

    return true;
}

static bool validate_elf(const struct xbks_ramdisk_file *file, const struct elf64_ehdr **out_header) {
    if (file == 0 || file->directory || file->data == 0 || file->size < sizeof(struct elf64_ehdr)) {
        return false;
    }

    const struct elf64_ehdr *header = file->data;
    if (header->e_ident[0] != 0x7f ||
        header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' ||
        header->e_ident[3] != 'F' ||
        header->e_ident[4] != 2 ||
        header->e_ident[5] != 1 ||
        header->e_machine != 62 ||
        header->e_phentsize != sizeof(struct elf64_phdr) ||
        header->e_phnum == 0 ||
        header->e_phoff > file->size ||
        header->e_phoff + (uint64_t)header->e_phnum * sizeof(struct elf64_phdr) > file->size) {
        return false;
    }

    *out_header = header;
    return true;
}

static bool load_elf_into_process(struct process *process, const struct xbks_ramdisk_file *file) {
    const struct elf64_ehdr *header = 0;

    if (!validate_elf(file, &header)) {
        return false;
    }

    const struct elf64_phdr *phdrs = (const struct elf64_phdr *)((const uint8_t *)file->data + header->e_phoff);

    for (uint16_t i = 0; i < header->e_phnum; ++i) {
        const struct elf64_phdr *phdr = &phdrs[i];

        if (phdr->p_type != 1) {
            continue;
        }

        if (phdr->p_memsz < phdr->p_filesz ||
            phdr->p_offset + phdr->p_filesz > file->size ||
            phdr->p_vaddr < USER_IMAGE_MIN ||
            phdr->p_vaddr + phdr->p_memsz >= XBKS_USER_TOP) {
            return false;
        }

        const uint64_t flags = XBKS_PAGE_WRITABLE;
        if (!map_user_range(process->cr3, (uintptr_t)phdr->p_vaddr, (size_t)phdr->p_memsz, flags)) {
            return false;
        }

        if (!write_process_memory(process->cr3,
                                  (uintptr_t)phdr->p_vaddr,
                                  (const uint8_t *)file->data + phdr->p_offset,
                                  (size_t)phdr->p_filesz)) {
            return false;
        }
    }

    process->entry = (uintptr_t)header->e_entry;
    process->brk = 0x800000;
    return true;
}

static bool push_bytes(uintptr_t cr3, uintptr_t *stack, const void *data, size_t size, uintptr_t *out_address) {
    *stack -= size;
    if ((size != 0 && !user_range_is_valid(*stack, size)) ||
        !write_process_memory(cr3, *stack, data, size)) {
        return false;
    }

    if (out_address != 0) {
        *out_address = *stack;
    }

    return true;
}

static bool push_u64(uintptr_t cr3, uintptr_t *stack, uint64_t value) {
    return push_bytes(cr3, stack, &value, sizeof(value), 0);
}

static bool build_user_stack(
    struct process *process,
    int argc,
    const char *argv[],
    int envc,
    const char *envp[]
) {
    uintptr_t stack = XBKS_USER_STACK_TOP;
    uintptr_t argv_addresses[MAX_ARGS];
    uintptr_t env_addresses[MAX_ENV];

    if (!map_user_range(process->cr3,
                        XBKS_USER_STACK_TOP - XBKS_USER_STACK_SIZE,
                        XBKS_USER_STACK_SIZE,
                        XBKS_PAGE_WRITABLE)) {
        return false;
    }

    for (int i = envc - 1; i >= 0; --i) {
        if (!push_bytes(process->cr3, &stack, envp[i], strlen(envp[i]) + 1u, &env_addresses[i])) {
            return false;
        }
    }

    for (int i = argc - 1; i >= 0; --i) {
        if (!push_bytes(process->cr3, &stack, argv[i], strlen(argv[i]) + 1u, &argv_addresses[i])) {
            return false;
        }
    }

    stack &= ~(uintptr_t)0xf;

    if (!push_u64(process->cr3, &stack, 0)) {
        return false;
    }

    for (int i = envc - 1; i >= 0; --i) {
        if (!push_u64(process->cr3, &stack, env_addresses[i])) {
            return false;
        }
    }

    if (!push_u64(process->cr3, &stack, 0)) {
        return false;
    }

    for (int i = argc - 1; i >= 0; --i) {
        if (!push_u64(process->cr3, &stack, argv_addresses[i])) {
            return false;
        }
    }

    if (!push_u64(process->cr3, &stack, (uint64_t)argc)) {
        return false;
    }

    process->stack = stack;
    return true;
}

static bool load_program(
    struct process *process,
    const char *path,
    int argc,
    const char *argv[],
    int envc,
    const char *envp[]
) {
    struct xbks_ramdisk_file file;

    if (!xbks_ramdisk_find(path, &file)) {
        return false;
    }

    if (!file.executable || file.directory) {
        return false;
    }

    process->cr3 = xbks_create_kernel_mapped_pml4();
    if (process->cr3 == 0) {
        return false;
    }

    if (load_elf_into_process(process, &file) && build_user_stack(process, argc, argv, envc, envp)) {
        return true;
    }

    xbks_vmm_destroy_user_space(process->cr3, XBKS_USER_TOP);
    process->cr3 = 0;
    return false;
}

static uint64_t syscall_read(uint64_t fd, uint64_t buffer_address, uint64_t count) {
    if (!user_range_is_valid((uintptr_t)buffer_address, (size_t)count)) {
        return negative_errno(XBKS_EFAULT);
    }

    if (fd >= MAX_FDS || current_process->fds[fd].type == FD_NONE) {
        return negative_errno(XBKS_EBADF);
    }

    struct process_fd *file = &current_process->fds[fd];
    char *buffer = (char *)(uintptr_t)buffer_address;

    if (file->type == FD_STDIN) {
        for (uint64_t i = 0; i < count; ++i) {
            buffer[i] = xbks_keyboard_read_char_blocking();
        }
        return count;
    }

    if (file->type != FD_FILE) {
        return negative_errno(XBKS_EISDIR);
    }

    if (file->offset >= file->file.size) {
        return 0;
    }

    size_t available = file->file.size - (size_t)file->offset;
    if (count < available) {
        available = (size_t)count;
    }

    memcpy(buffer, (const uint8_t *)file->file.data + file->offset, available);
    file->offset += available;
    return available;
}

static uint64_t syscall_write(uint64_t fd, uint64_t buffer_address, uint64_t count) {
    if (fd != 1 && fd != 2) {
        return negative_errno(XBKS_EBADF);
    }

    if (!user_range_is_valid((uintptr_t)buffer_address, (size_t)count)) {
        return negative_errno(XBKS_EFAULT);
    }

    xbks_console_write((const char *)(uintptr_t)buffer_address, (size_t)count);
    return count;
}

static uint64_t syscall_open(uint64_t path_address) {
    char path[MAX_STRING];
    struct xbks_ramdisk_file file;
    const int fd = allocate_fd(current_process);

    if (fd < 0) {
        return negative_errno(XBKS_EMFILE);
    }

    if (!copy_user_string(path, sizeof(path), (const char *)(uintptr_t)path_address)) {
        return negative_errno(XBKS_EFAULT);
    }

    if (!xbks_ramdisk_find(path, &file)) {
        return negative_errno(XBKS_ENOENT);
    }

    current_process->fds[fd].type = file.directory ? FD_DIR : FD_FILE;
    current_process->fds[fd].file = file;
    current_process->fds[fd].offset = 0;
    return (uint64_t)fd;
}

static uint64_t syscall_close(uint64_t fd) {
    if (fd >= MAX_FDS || fd <= 2 || current_process->fds[fd].type == FD_NONE) {
        return negative_errno(XBKS_EBADF);
    }

    memset(&current_process->fds[fd], 0, sizeof(current_process->fds[fd]));
    return 0;
}

static uint64_t syscall_lseek(uint64_t fd, uint64_t offset, uint64_t whence) {
    if (fd >= MAX_FDS || current_process->fds[fd].type != FD_FILE) {
        return negative_errno(XBKS_EBADF);
    }

    struct process_fd *file = &current_process->fds[fd];
    uint64_t next_offset = 0;

    if (whence == 0) {
        next_offset = offset;
    } else if (whence == 1) {
        next_offset = file->offset + offset;
    } else if (whence == 2) {
        next_offset = file->file.size + offset;
    } else {
        return negative_errno(XBKS_EINVAL);
    }

    file->offset = next_offset;
    return next_offset;
}

static uint64_t syscall_getdents64(uint64_t fd, uint64_t dirent_address, uint64_t count) {
    if (fd >= MAX_FDS || current_process->fds[fd].type != FD_DIR) {
        return negative_errno(XBKS_EBADF);
    }

    if (!user_range_is_valid((uintptr_t)dirent_address, (size_t)count)) {
        return negative_errno(XBKS_EFAULT);
    }

    struct process_fd *directory = &current_process->fds[fd];
    uint8_t *buffer = (uint8_t *)(uintptr_t)dirent_address;
    size_t written = 0;

    for (;;) {
        struct xbks_ramdisk_file child;
        if (!xbks_ramdisk_read_dir(directory->file.path, (size_t)directory->offset, &child)) {
            break;
        }

        const char *name = child.path;
        for (const char *cursor = child.path; *cursor != '\0'; ++cursor) {
            if (*cursor == '/') {
                name = cursor + 1;
            }
        }

        const size_t name_length = strlen(name) + 1u;
        const size_t reclen = align_up(sizeof(struct linux_dirent64) + name_length, 8);
        if (written + reclen > count) {
            break;
        }

        struct linux_dirent64 *dirent = (struct linux_dirent64 *)(void *)(buffer + written);
        dirent->d_ino = directory->offset + 1u;
        dirent->d_off = (int64_t)(directory->offset + 1u);
        dirent->d_reclen = (uint16_t)reclen;
        dirent->d_type = child.directory ? DT_DIR : DT_REG;
        copy_kernel_string(dirent->d_name, name_length, name);

        written += reclen;
        ++directory->offset;
    }

    return written;
}

static uint64_t syscall_access(uint64_t path_address) {
    char path[MAX_STRING];
    struct xbks_ramdisk_file file;

    if (!copy_user_string(path, sizeof(path), (const char *)(uintptr_t)path_address)) {
        return negative_errno(XBKS_EFAULT);
    }

    return xbks_ramdisk_find(path, &file) ? 0 : negative_errno(XBKS_ENOENT);
}

static uint64_t syscall_getcwd(uint64_t buffer_address, uint64_t size) {
    static const char cwd[] = "/";

    if (size < sizeof(cwd)) {
        return negative_errno(XBKS_ERANGE);
    }

    if (!user_range_is_valid((uintptr_t)buffer_address, sizeof(cwd))) {
        return negative_errno(XBKS_EFAULT);
    }

    memcpy((void *)(uintptr_t)buffer_address, cwd, sizeof(cwd));
    return buffer_address;
}

struct xbks_uname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

static uint64_t syscall_uname(uint64_t buffer_address) {
    if (!user_range_is_valid((uintptr_t)buffer_address, sizeof(struct xbks_uname))) {
        return negative_errno(XBKS_EFAULT);
    }

    struct xbks_uname *name = (struct xbks_uname *)(uintptr_t)buffer_address;
    copy_kernel_string(name->sysname, sizeof(name->sysname), "XiaobaiOS");
    copy_kernel_string(name->nodename, sizeof(name->nodename), "localhost");
    copy_kernel_string(name->release, sizeof(name->release), "0.1.0");
    copy_kernel_string(name->version, sizeof(name->version), "xbks ring3");
    copy_kernel_string(name->machine, sizeof(name->machine), "x86_64");
    copy_kernel_string(name->domainname, sizeof(name->domainname), "localdomain");
    return 0;
}

static uint64_t syscall_brk(uint64_t requested_break) {
    if (requested_break == 0) {
        return current_process->brk;
    }

    if (requested_break < current_process->brk || requested_break >= 0x10000000) {
        return current_process->brk;
    }

    const uintptr_t old_end = align_up(current_process->brk, XBKS_PAGE_SIZE);
    const uintptr_t new_end = align_up((uintptr_t)requested_break, XBKS_PAGE_SIZE);

    for (uintptr_t page = old_end; page < new_end; page += XBKS_PAGE_SIZE) {
        if (!map_user_page(current_process->cr3, page, XBKS_PAGE_WRITABLE)) {
            return current_process->brk;
        }
    }

    current_process->brk = (uintptr_t)requested_break;
    return current_process->brk;
}

static uint64_t syscall_execve(struct xbks_syscall_frame *frame) {
    char path[MAX_STRING];
    char argv_storage[MAX_ARGS][MAX_STRING];
    char env_storage[MAX_ENV][MAX_STRING];
    const char *argv[MAX_ARGS];
    const char *envp[MAX_ENV];
    int argc = 0;
    int envc = 0;

    if (!copy_user_string(path, sizeof(path), (const char *)(uintptr_t)frame->rdi) ||
        !copy_user_string_array(argv_storage, MAX_ARGS - 1, &argc, (char *const *)(uintptr_t)frame->rsi) ||
        !copy_user_string_array(env_storage, MAX_ENV - 1, &envc, (char *const *)(uintptr_t)frame->rdx)) {
        return negative_errno(XBKS_EFAULT);
    }

    for (int i = 0; i < argc; ++i) {
        argv[i] = argv_storage[i];
    }
    argv[argc] = 0;

    for (int i = 0; i < envc; ++i) {
        envp[i] = env_storage[i];
    }
    envp[envc] = 0;

    struct process next = *current_process;
    next.cr3 = 0;

    if (!load_program(&next, path, argc, argv, envc, envp)) {
        return negative_errno(XBKS_ENOEXEC);
    }

    const uintptr_t old_cr3 = current_process->cr3;
    current_process->cr3 = next.cr3;
    current_process->entry = next.entry;
    current_process->stack = next.stack;
    current_process->brk = next.brk;
    xbks_write_cr3(current_process->cr3);
    xbks_vmm_destroy_user_space(old_cr3, XBKS_USER_TOP);

    frame->user_rip = current_process->entry;
    frame->user_rsp = current_process->stack;
    return 0;
}

static uint64_t syscall_vfork(struct xbks_syscall_frame *frame) {
    struct process *child = allocate_process();

    if (child == 0) {
        return negative_errno(XBKS_EAGAIN);
    }

    child->ppid = current_process->pid;
    child->parent = current_process;
    child->vfork_parent = current_process;
    child->cr3 = xbks_create_kernel_mapped_pml4();
    if (child->cr3 == 0 || !xbks_vmm_clone_user_space(current_process->cr3, child->cr3, XBKS_USER_TOP)) {
        if (child->cr3 != 0) {
            xbks_vmm_destroy_user_space(child->cr3, XBKS_USER_TOP);
        }
        memset(child, 0, sizeof(*child));
        return negative_errno(XBKS_ENOMEM);
    }
    child->entry = current_process->entry;
    child->stack = current_process->stack;
    child->brk = current_process->brk;
    child->saved_parent_frame = *frame;
    child->saved_parent_frame.rax = (uint64_t)child->pid;

    for (size_t i = 0; i < MAX_FDS; ++i) {
        child->fds[i] = current_process->fds[i];
    }

    current_process->state = PROCESS_BLOCKED;
    current_process = child;
    xbks_write_cr3(child->cr3);
    return 0;
}

static uint64_t syscall_wait4(uint64_t pid, uint64_t status_address) {
    for (size_t i = 0; i < MAX_PROCESSES; ++i) {
        struct process *child = &processes[i];
        if (child->state == PROCESS_ZOMBIE &&
            child->parent == current_process &&
            (pid == (uint64_t)-1 || pid == (uint64_t)child->pid)) {
            if (status_address != 0) {
                if (!user_range_is_valid((uintptr_t)status_address, sizeof(int))) {
                    return negative_errno(XBKS_EFAULT);
                }
                *(int *)(uintptr_t)status_address = child->exit_status << 8;
            }

            const int child_pid = child->pid;
            xbks_vmm_destroy_user_space(child->cr3, XBKS_USER_TOP);
            memset(child, 0, sizeof(*child));
            return (uint64_t)child_pid;
        }
    }

    return negative_errno(XBKS_ECHILD);
}

static uint64_t syscall_exit(struct xbks_syscall_frame *frame, uint64_t status) {
    const int child_pid = current_process->pid;

    current_process->exit_status = (int)(status & 0xffu);
    current_process->state = PROCESS_ZOMBIE;

    if (current_process->vfork_parent != 0) {
        struct process *parent = current_process->vfork_parent;
        *frame = current_process->saved_parent_frame;
        frame->rax = (uint64_t)child_pid;
        parent->state = PROCESS_RUNNING;
        current_process = parent;
        xbks_write_cr3(parent->cr3);
        return (uint64_t)child_pid;
    }

    xbks_log_printf(XBKS_LOG_INFO, "init process exited with status %d", current_process->exit_status);
    for (;;) {
        xbks_hlt();
    }
}

uint64_t xbks_process_syscall_dispatch(struct xbks_syscall_frame *frame) {
    switch (frame->rax) {
    case XBKS_SYS_READ:
        return syscall_read(frame->rdi, frame->rsi, frame->rdx);
    case XBKS_SYS_WRITE:
        return syscall_write(frame->rdi, frame->rsi, frame->rdx);
    case XBKS_SYS_OPEN:
        return syscall_open(frame->rdi);
    case XBKS_SYS_OPENAT:
        (void)frame->rdi;
        return syscall_open(frame->rsi);
    case XBKS_SYS_CLOSE:
        return syscall_close(frame->rdi);
    case XBKS_SYS_LSEEK:
        return syscall_lseek(frame->rdi, frame->rsi, frame->rdx);
    case XBKS_SYS_ACCESS:
    case XBKS_SYS_FACCESSAT:
        return syscall_access(frame->rdi == (uint64_t)AT_FDCWD ? frame->rsi : frame->rdi);
    case XBKS_SYS_GETDENTS64:
        return syscall_getdents64(frame->rdi, frame->rsi, frame->rdx);
    case XBKS_SYS_BRK:
        return syscall_brk(frame->rdi);
    case XBKS_SYS_GETCWD:
        return syscall_getcwd(frame->rdi, frame->rsi);
    case XBKS_SYS_UNAME:
        return syscall_uname(frame->rdi);
    case XBKS_SYS_GETPID:
    case XBKS_SYS_GETTID:
        return (uint64_t)current_process->pid;
    case XBKS_SYS_GETPPID:
        return (uint64_t)current_process->ppid;
    case XBKS_SYS_GETUID:
    case XBKS_SYS_GETGID:
    case XBKS_SYS_GETEUID:
    case XBKS_SYS_GETEGID:
        return 0;
    case XBKS_SYS_VFORK:
        return syscall_vfork(frame);
    case XBKS_SYS_EXECVE:
        return syscall_execve(frame);
    case XBKS_SYS_WAIT4:
        return syscall_wait4(frame->rdi, frame->rsi);
    case XBKS_SYS_EXIT:
    case XBKS_SYS_EXIT_GROUP:
        return syscall_exit(frame, frame->rdi);
    case XBKS_SYS_SCHED_YIELD:
        return 0;
    default:
        return negative_errno(XBKS_ENOSYS);
    }
}

bool xbks_process_start_init(const struct xbks_boot_info *boot_info) {
    static const char *argv[] = { "xsh", 0 };
    static const char *envp[] = { "PATH=/bin:/sbin:/usr/bin", 0 };

    if (!xbks_ramdisk_init(boot_info->modules)) {
        xbks_log_write(XBKS_LOG_WARN, "no RAMDISK module found; falling back to kernel shell");
        return false;
    }

    struct process *init = allocate_process();
    if (init == 0) {
        xbks_log_write(XBKS_LOG_ERROR, "failed to allocate init process");
        return false;
    }

    if (!load_program(init, "/bin/xsh", 1, argv, 1, envp)) {
        xbks_log_write(XBKS_LOG_ERROR, "failed to load /bin/xsh as ring3 init");
        memset(init, 0, sizeof(*init));
        return false;
    }

    current_process = init;
    xbks_keyboard_init();
    xbks_log_write(XBKS_LOG_INFO, "entering ring3 init process /bin/xsh");
    xbks_write_cr3(init->cr3);
    xbks_x86_64_enter_user(init->entry, init->stack);
    return true;
}
