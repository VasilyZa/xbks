#include <xbks/arch.h>
#include <xbks/boot_info.h>
#include <xbks/io.h>
#include <xbks/kernel.h>
#include <xbks/log.h>
#include <xbks/mm.h>
#include <xbks/panic.h>
#include <xbks/process.h>
#include <xbks/shell.h>
#include <xbks/tty.h>

XBKS_NORETURN void xbks_kernel_main(const struct xbks_boot_info *boot_info) {
    xbks_log_init();

    if (boot_info == 0) {
        xbks_panic("boot information is null");
    }

    xbks_log_write(XBKS_LOG_INFO, "xbks kernel entered through Limine");
    xbks_arch_early_init();
    xbks_mm_init(boot_info);
    (void)xbks_tty_init(boot_info->framebuffer, boot_info->modules);
    xbks_arch_late_init();
    (void)xbks_process_start_init(boot_info);
    xbks_shell_run(boot_info);

    for (;;) {
        xbks_hlt();
    }
}
