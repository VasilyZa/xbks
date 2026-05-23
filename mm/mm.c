#include <limine.h>
#include <xbks/log.h>
#include <xbks/mm.h>
#include <xbks/string.h>

struct free_frame {
    struct free_frame *next;
};

static uintptr_t hhdm_offset_value;
static uintptr_t kernel_cr3_value;
static struct free_frame *free_frames;
static size_t free_frame_count;

static uintptr_t align_up(uintptr_t value, uintptr_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uintptr_t align_down(uintptr_t value, uintptr_t alignment) {
    return value & ~(alignment - 1u);
}

uintptr_t xbks_read_cr3(void) {
    uintptr_t value;

    __asm__ volatile ("mov %%cr3, %0" : "=r" (value) :: "memory");
    return value;
}

void xbks_write_cr3(uintptr_t cr3) {
    __asm__ volatile ("mov %0, %%cr3" :: "r" (cr3) : "memory");
}

uintptr_t xbks_hhdm_offset(void) {
    return hhdm_offset_value;
}

void *xbks_phys_to_virt(uintptr_t physical_address) {
    return (void *)(physical_address + hhdm_offset_value);
}

uintptr_t xbks_virt_to_phys(const void *virtual_address) {
    return (uintptr_t)virtual_address - hhdm_offset_value;
}

uintptr_t xbks_kernel_cr3(void) {
    return kernel_cr3_value;
}

uintptr_t xbks_pmm_alloc_frame(void) {
    if (free_frames == 0) {
        return 0;
    }

    struct free_frame *frame = free_frames;
    free_frames = frame->next;
    --free_frame_count;

    const uintptr_t physical = xbks_virt_to_phys(frame);
    memset(frame, 0, XBKS_PAGE_SIZE);
    return physical;
}

void xbks_pmm_free_frame(uintptr_t frame) {
    if (frame == 0) {
        return;
    }

    struct free_frame *node = xbks_phys_to_virt(align_down(frame, XBKS_PAGE_SIZE));
    node->next = free_frames;
    free_frames = node;
    ++free_frame_count;
}

static void add_usable_range(uintptr_t base, uintptr_t length) {
    const uintptr_t start = align_up(base, XBKS_PAGE_SIZE);
    const uintptr_t end = align_down(base + length, XBKS_PAGE_SIZE);

    for (uintptr_t frame = start; frame + XBKS_PAGE_SIZE <= end; frame += XBKS_PAGE_SIZE) {
        xbks_pmm_free_frame(frame);
    }
}

uintptr_t xbks_create_kernel_mapped_pml4(void) {
    const uintptr_t pml4_frame = xbks_pmm_alloc_frame();

    if (pml4_frame == 0) {
        return 0;
    }

    uint64_t *new_pml4 = xbks_phys_to_virt(pml4_frame);
    const uint64_t *kernel_pml4 = xbks_phys_to_virt(kernel_cr3_value & ~(uintptr_t)0xfff);

    for (size_t i = 256; i < 512; ++i) {
        new_pml4[i] = kernel_pml4[i];
    }

    return pml4_frame;
}

static uint64_t *page_table_from_entry(uint64_t entry) {
    return xbks_phys_to_virt((uintptr_t)(entry & 0x000ffffffffff000ull));
}

static bool ensure_next_table(uint64_t *table, size_t index, uint64_t flags, uint64_t **out_next) {
    if ((table[index] & XBKS_PAGE_PRESENT) == 0) {
        const uintptr_t frame = xbks_pmm_alloc_frame();

        if (frame == 0) {
            return false;
        }

        table[index] = (uint64_t)frame | flags | XBKS_PAGE_PRESENT | XBKS_PAGE_WRITABLE | XBKS_PAGE_USER;
    }

    *out_next = page_table_from_entry(table[index]);
    return true;
}

bool xbks_vmm_map_page(
    uintptr_t pml4_frame,
    uintptr_t virtual_address,
    uintptr_t physical_address,
    uint64_t flags
) {
    const size_t pml4_index = (virtual_address >> 39) & 0x1ffu;
    const size_t pdpt_index = (virtual_address >> 30) & 0x1ffu;
    const size_t pd_index = (virtual_address >> 21) & 0x1ffu;
    const size_t pt_index = (virtual_address >> 12) & 0x1ffu;
    uint64_t *pml4 = xbks_phys_to_virt(pml4_frame);
    uint64_t *pdpt = 0;
    uint64_t *pd = 0;
    uint64_t *pt = 0;

    if ((virtual_address & (XBKS_PAGE_SIZE - 1u)) != 0 ||
        (physical_address & (XBKS_PAGE_SIZE - 1u)) != 0) {
        return false;
    }

    if (!ensure_next_table(pml4, pml4_index, flags, &pdpt) ||
        !ensure_next_table(pdpt, pdpt_index, flags, &pd) ||
        !ensure_next_table(pd, pd_index, flags, &pt)) {
        return false;
    }

    if ((pt[pt_index] & XBKS_PAGE_PRESENT) != 0) {
        return false;
    }

    pt[pt_index] = (uint64_t)physical_address | flags | XBKS_PAGE_PRESENT;
    return true;
}

bool xbks_vmm_translate(uintptr_t pml4_frame, uintptr_t virtual_address, uintptr_t *out_physical_address) {
    const size_t pml4_index = (virtual_address >> 39) & 0x1ffu;
    const size_t pdpt_index = (virtual_address >> 30) & 0x1ffu;
    const size_t pd_index = (virtual_address >> 21) & 0x1ffu;
    const size_t pt_index = (virtual_address >> 12) & 0x1ffu;
    const uint64_t *pml4 = xbks_phys_to_virt(pml4_frame);

    if (out_physical_address == 0 || (pml4[pml4_index] & XBKS_PAGE_PRESENT) == 0) {
        return false;
    }

    const uint64_t *pdpt = page_table_from_entry(pml4[pml4_index]);
    if ((pdpt[pdpt_index] & XBKS_PAGE_PRESENT) == 0) {
        return false;
    }

    const uint64_t *pd = page_table_from_entry(pdpt[pdpt_index]);
    if ((pd[pd_index] & XBKS_PAGE_PRESENT) == 0) {
        return false;
    }

    const uint64_t *pt = page_table_from_entry(pd[pd_index]);
    if ((pt[pt_index] & XBKS_PAGE_PRESENT) == 0) {
        return false;
    }

    *out_physical_address = (uintptr_t)(pt[pt_index] & 0x000ffffffffff000ull) |
        (virtual_address & (XBKS_PAGE_SIZE - 1u));
    return true;
}

static uint64_t clone_page_flags(uint64_t source_entry) {
    return source_entry & 0xfffull;
}

bool xbks_vmm_clone_user_space(uintptr_t source_pml4_frame, uintptr_t dest_pml4_frame, uintptr_t user_top) {
    const uint64_t *source_pml4 = xbks_phys_to_virt(source_pml4_frame);

    for (size_t pml4_index = 0; pml4_index < 256; ++pml4_index) {
        if ((source_pml4[pml4_index] & XBKS_PAGE_PRESENT) == 0) {
            continue;
        }

        const uint64_t *source_pdpt = page_table_from_entry(source_pml4[pml4_index]);
        for (size_t pdpt_index = 0; pdpt_index < 512; ++pdpt_index) {
            if ((source_pdpt[pdpt_index] & XBKS_PAGE_PRESENT) == 0) {
                continue;
            }

            const uint64_t *source_pd = page_table_from_entry(source_pdpt[pdpt_index]);
            for (size_t pd_index = 0; pd_index < 512; ++pd_index) {
                if ((source_pd[pd_index] & XBKS_PAGE_PRESENT) == 0) {
                    continue;
                }

                const uint64_t *source_pt = page_table_from_entry(source_pd[pd_index]);
                for (size_t pt_index = 0; pt_index < 512; ++pt_index) {
                    const uint64_t source_entry = source_pt[pt_index];
                    if ((source_entry & XBKS_PAGE_PRESENT) == 0) {
                        continue;
                    }

                    const uintptr_t virtual_address =
                        ((uintptr_t)pml4_index << 39) |
                        ((uintptr_t)pdpt_index << 30) |
                        ((uintptr_t)pd_index << 21) |
                        ((uintptr_t)pt_index << 12);

                    if (virtual_address >= user_top) {
                        continue;
                    }

                    const uintptr_t source_physical = (uintptr_t)(source_entry & 0x000ffffffffff000ull);
                    const uintptr_t dest_physical = xbks_pmm_alloc_frame();
                    if (dest_physical == 0) {
                        return false;
                    }

                    memcpy(xbks_phys_to_virt(dest_physical), xbks_phys_to_virt(source_physical), XBKS_PAGE_SIZE);

                    if (!xbks_vmm_map_page(
                            dest_pml4_frame,
                            virtual_address,
                            dest_physical,
                            clone_page_flags(source_entry))) {
                        xbks_pmm_free_frame(dest_physical);
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

void xbks_vmm_destroy_user_space(uintptr_t pml4_frame, uintptr_t user_top) {
    if (pml4_frame == 0) {
        return;
    }

    uint64_t *pml4 = xbks_phys_to_virt(pml4_frame);

    for (size_t pml4_index = 0; pml4_index < 256; ++pml4_index) {
        if ((pml4[pml4_index] & XBKS_PAGE_PRESENT) == 0) {
            continue;
        }

        uint64_t *pdpt = page_table_from_entry(pml4[pml4_index]);
        for (size_t pdpt_index = 0; pdpt_index < 512; ++pdpt_index) {
            if ((pdpt[pdpt_index] & XBKS_PAGE_PRESENT) == 0) {
                continue;
            }

            uint64_t *pd = page_table_from_entry(pdpt[pdpt_index]);
            for (size_t pd_index = 0; pd_index < 512; ++pd_index) {
                if ((pd[pd_index] & XBKS_PAGE_PRESENT) == 0) {
                    continue;
                }

                uint64_t *pt = page_table_from_entry(pd[pd_index]);
                for (size_t pt_index = 0; pt_index < 512; ++pt_index) {
                    const uint64_t entry = pt[pt_index];
                    if ((entry & XBKS_PAGE_PRESENT) == 0) {
                        continue;
                    }

                    const uintptr_t virtual_address =
                        ((uintptr_t)pml4_index << 39) |
                        ((uintptr_t)pdpt_index << 30) |
                        ((uintptr_t)pd_index << 21) |
                        ((uintptr_t)pt_index << 12);

                    if (virtual_address < user_top) {
                        xbks_pmm_free_frame((uintptr_t)(entry & 0x000ffffffffff000ull));
                        pt[pt_index] = 0;
                    }
                }

                xbks_pmm_free_frame((uintptr_t)(pd[pd_index] & 0x000ffffffffff000ull));
                pd[pd_index] = 0;
            }

            xbks_pmm_free_frame((uintptr_t)(pdpt[pdpt_index] & 0x000ffffffffff000ull));
            pdpt[pdpt_index] = 0;
        }

        xbks_pmm_free_frame((uintptr_t)(pml4[pml4_index] & 0x000ffffffffff000ull));
        pml4[pml4_index] = 0;
    }

    xbks_pmm_free_frame(pml4_frame);
}

static const char *memmap_type_name(uint64_t type) {
    switch (type) {
    case LIMINE_MEMMAP_USABLE:
        return "usable";
    case LIMINE_MEMMAP_RESERVED:
        return "reserved";
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
        return "acpi-reclaim";
    case LIMINE_MEMMAP_ACPI_NVS:
        return "acpi-nvs";
    case LIMINE_MEMMAP_BAD_MEMORY:
        return "bad";
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        return "bootloader";
    case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
        return "kernel";
    case LIMINE_MEMMAP_FRAMEBUFFER:
        return "framebuffer";
    case LIMINE_MEMMAP_RESERVED_MAPPED:
        return "reserved-mapped";
    default:
        return "unknown";
    }
}

void xbks_mm_init(const struct xbks_boot_info *boot_info) {
    kernel_cr3_value = xbks_read_cr3() & ~(uintptr_t)0xfff;

    if (boot_info->hhdm != 0) {
        hhdm_offset_value = (uintptr_t)boot_info->hhdm->offset;
        xbks_log_printf(XBKS_LOG_INFO, "hhdm offset: 0x%llx", boot_info->hhdm->offset);
    } else {
        xbks_log_write(XBKS_LOG_WARN, "hhdm response unavailable");
        return;
    }

    if (boot_info->memmap == 0) {
        xbks_log_write(XBKS_LOG_WARN, "memory map response unavailable");
        return;
    }

    xbks_log_printf(XBKS_LOG_INFO, "memory map entries: %llu", boot_info->memmap->entry_count);

    for (uint64_t i = 0; i < boot_info->memmap->entry_count; ++i) {
        const struct limine_memmap_entry *entry = boot_info->memmap->entries[i];
        if (i < 8) {
            xbks_log_printf(
                XBKS_LOG_DEBUG,
                "mem[%llu] base=0x%llx length=0x%llx type=%s",
                i,
                entry->base,
                entry->length,
                memmap_type_name(entry->type)
            );
        }

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            add_usable_range((uintptr_t)entry->base, (uintptr_t)entry->length);
        }
    }

    xbks_log_printf(XBKS_LOG_INFO, "PMM initialized with %zu free frames", free_frame_count);
}
