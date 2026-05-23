#include <xbks/elf.h>
#include <xbks/log.h>
#include <xbks/string.h>

enum {
    EI_NIDENT = 16,
    EI_CLASS = 4,
    EI_DATA = 5,
    ELFCLASS64 = 2,
    ELFDATA2LSB = 1,
    ET_EXEC = 2,
    ET_DYN = 3,
    EM_X86_64 = 62,
    PT_LOAD = 1,
};

struct elf64_ehdr {
    unsigned char e_ident[EI_NIDENT];
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

static bool add_overflows_u64(uint64_t lhs, uint64_t rhs) {
    return UINT64_MAX - lhs < rhs;
}

static bool validate_elf_header(const struct xbks_ramdisk_file *file, const struct elf64_ehdr **out_header) {
    if (file == 0 || file->data == 0 || file->directory || file->size < sizeof(struct elf64_ehdr)) {
        return false;
    }

    const struct elf64_ehdr *header = file->data;

    if (header->e_ident[0] != 0x7f ||
        header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' ||
        header->e_ident[3] != 'F' ||
        header->e_ident[EI_CLASS] != ELFCLASS64 ||
        header->e_ident[EI_DATA] != ELFDATA2LSB ||
        (header->e_type != ET_EXEC && header->e_type != ET_DYN) ||
        header->e_machine != EM_X86_64 ||
        header->e_phentsize != sizeof(struct elf64_phdr) ||
        header->e_phnum == 0) {
        return false;
    }

    if (header->e_phoff > file->size ||
        (uint64_t)header->e_phnum > (UINT64_MAX - header->e_phoff) / sizeof(struct elf64_phdr) ||
        header->e_phoff + (uint64_t)header->e_phnum * sizeof(struct elf64_phdr) > file->size) {
        return false;
    }

    *out_header = header;
    return true;
}

bool xbks_elf_load_image(
    const struct xbks_ramdisk_file *file,
    uintptr_t arena_base,
    size_t arena_size,
    struct xbks_elf_image *out
) {
    const struct elf64_ehdr *header = 0;
    const struct elf64_phdr *program_headers = 0;
    uint64_t min_vaddr = UINT64_MAX;
    uint64_t max_vaddr = 0;
    bool found_load_segment = false;

    if (out == 0 || arena_base == 0 || arena_size == 0 || !validate_elf_header(file, &header)) {
        return false;
    }

    program_headers = (const struct elf64_phdr *)((const uint8_t *)file->data + header->e_phoff);

    for (uint16_t i = 0; i < header->e_phnum; ++i) {
        const struct elf64_phdr *phdr = &program_headers[i];

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        if (phdr->p_memsz < phdr->p_filesz ||
            add_overflows_u64(phdr->p_offset, phdr->p_filesz) ||
            phdr->p_offset + phdr->p_filesz > file->size ||
            add_overflows_u64(phdr->p_vaddr, phdr->p_memsz)) {
            return false;
        }

        if (phdr->p_vaddr < min_vaddr) {
            min_vaddr = phdr->p_vaddr;
        }

        if (phdr->p_vaddr + phdr->p_memsz > max_vaddr) {
            max_vaddr = phdr->p_vaddr + phdr->p_memsz;
        }

        found_load_segment = true;
    }

    if (!found_load_segment || max_vaddr < min_vaddr || max_vaddr - min_vaddr > arena_size) {
        return false;
    }

    if (header->e_entry < min_vaddr || header->e_entry >= max_vaddr) {
        return false;
    }

    memset((void *)arena_base, 0, arena_size);

    for (uint16_t i = 0; i < header->e_phnum; ++i) {
        const struct elf64_phdr *phdr = &program_headers[i];

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        const uint64_t relative_vaddr = phdr->p_vaddr - min_vaddr;

        if (relative_vaddr > arena_size || phdr->p_memsz > arena_size - relative_vaddr) {
            return false;
        }

        memcpy((void *)(arena_base + (uintptr_t)relative_vaddr),
               (const uint8_t *)file->data + phdr->p_offset,
               (size_t)phdr->p_filesz);
    }

    out->entry = arena_base + (uintptr_t)(header->e_entry - min_vaddr);
    out->base = arena_base;
    out->size = (size_t)(max_vaddr - min_vaddr);

    return true;
}
