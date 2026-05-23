#include <xbks/compiler.h>
#include <xbks/gdt.h>
#include <xbks/types.h>

enum {
    GDT_KERNEL_CODE = 0x08,
    GDT_KERNEL_DATA = 0x10,
    GDT_USER_DATA = 0x20,
    GDT_USER_CODE = 0x28,
};

struct gdt_pointer {
    uint16_t limit;
    uint64_t base;
} XBKS_PACKED;

extern void xbks_x86_64_gdt_load(const struct gdt_pointer *pointer);

static uint64_t gdt[] XBKS_ALIGNED(16) = {
    0x0000000000000000ull,
    0x00af9a000000ffffull,
    0x00cf92000000ffffull,
    0x0000000000000000ull,
    0x00cff2000000ffffull,
    0x00affa000000ffffull,
};

void xbks_gdt_init(void) {
    const struct gdt_pointer pointer = {
        .limit = (uint16_t)(sizeof(gdt) - 1),
        .base = (uint64_t)(uintptr_t)gdt,
    };

    (void)GDT_KERNEL_CODE;
    (void)GDT_KERNEL_DATA;
    (void)GDT_USER_DATA;
    (void)GDT_USER_CODE;

    xbks_x86_64_gdt_load(&pointer);
}
