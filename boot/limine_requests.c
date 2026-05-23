#include <limine.h>
#include <xbks/compiler.h>
#include <xbks/kernel.h>
#include <xbks/limine_requests.h>

XBKS_NORETURN void _start(void);
XBKS_NORETURN static void xbks_limine_entry(void);

XBKS_USED XBKS_SECTION(".limine_requests_start_marker")
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

XBKS_USED XBKS_SECTION(".limine_requests")
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

XBKS_USED XBKS_SECTION(".limine_requests")
static volatile struct limine_entry_point_request entry_point_request = {
    .id = LIMINE_ENTRY_POINT_REQUEST_ID,
    .revision = 0,
    .response = 0,
    .entry = _start,
};

XBKS_USED XBKS_SECTION(".limine_requests")
static volatile struct limine_bootloader_info_request bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

XBKS_USED XBKS_SECTION(".limine_requests")
static volatile struct limine_executable_cmdline_request executable_cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

XBKS_USED XBKS_SECTION(".limine_requests")
static volatile struct limine_firmware_type_request firmware_type_request = {
    .id = LIMINE_FIRMWARE_TYPE_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

XBKS_USED XBKS_SECTION(".limine_requests")
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

XBKS_USED XBKS_SECTION(".limine_requests")
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

XBKS_USED XBKS_SECTION(".limine_requests")
static volatile struct limine_executable_file_request executable_file_request = {
    .id = LIMINE_EXECUTABLE_FILE_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

XBKS_USED XBKS_SECTION(".limine_requests")
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

XBKS_USED XBKS_SECTION(".limine_requests")
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

XBKS_USED XBKS_SECTION(".limine_requests")
static volatile struct limine_paging_mode_request paging_mode_request = {
    .id = LIMINE_PAGING_MODE_REQUEST_ID,
    .revision = 1,
    .response = 0,
    .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
};

XBKS_USED XBKS_SECTION(".limine_requests_end_marker")
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

bool xbks_limine_base_revision_supported(void) {
    return LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision);
}

const struct xbks_boot_info *xbks_limine_boot_info(void) {
    static struct xbks_boot_info info;

    info.bootloader = bootloader_info_request.response;
    info.cmdline = executable_cmdline_request.response;
    info.firmware = firmware_type_request.response;
    info.hhdm = hhdm_request.response;
    info.memmap = memmap_request.response;
    info.executable_file = executable_file_request.response;
    info.framebuffer = framebuffer_request.response;
    info.modules = module_request.response;

    return &info;
}

void _start(void) {
    xbks_limine_entry();
}

static void xbks_limine_entry(void) {
    if (!xbks_limine_base_revision_supported()) {
        for (;;) {
            __asm__ volatile ("cli; hlt" ::: "memory");
        }
    }

    xbks_kernel_main(xbks_limine_boot_info());
}
