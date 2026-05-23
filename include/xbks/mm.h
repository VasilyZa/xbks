#ifndef XBKS_MM_H
#define XBKS_MM_H

#include <xbks/boot_info.h>
#include <xbks/types.h>

#define XBKS_PAGE_SIZE 4096ull

enum xbks_page_flags {
    XBKS_PAGE_PRESENT = 1u << 0,
    XBKS_PAGE_WRITABLE = 1u << 1,
    XBKS_PAGE_USER = 1u << 2,
};

void xbks_mm_init(const struct xbks_boot_info *boot_info);
uintptr_t xbks_hhdm_offset(void);
void *xbks_phys_to_virt(uintptr_t physical_address);
uintptr_t xbks_virt_to_phys(const void *virtual_address);
uintptr_t xbks_pmm_alloc_frame(void);
void xbks_pmm_free_frame(uintptr_t frame);
uintptr_t xbks_kernel_cr3(void);
uintptr_t xbks_read_cr3(void);
void xbks_write_cr3(uintptr_t cr3);
uintptr_t xbks_create_kernel_mapped_pml4(void);
bool xbks_vmm_map_page(uintptr_t pml4_frame, uintptr_t virtual_address, uintptr_t physical_address, uint64_t flags);
bool xbks_vmm_translate(uintptr_t pml4_frame, uintptr_t virtual_address, uintptr_t *out_physical_address);
bool xbks_vmm_clone_user_space(uintptr_t source_pml4_frame, uintptr_t dest_pml4_frame, uintptr_t user_top);
void xbks_vmm_destroy_user_space(uintptr_t pml4_frame, uintptr_t user_top);

#endif
