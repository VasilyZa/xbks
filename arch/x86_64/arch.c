#include <xbks/arch.h>
#include <xbks/gdt.h>
#include <xbks/log.h>
#include <xbks/syscall.h>

void xbks_arch_early_init(void) {
    xbks_log_write(XBKS_LOG_INFO, "x86_64 early architecture init");
    xbks_gdt_init();
    xbks_log_write(XBKS_LOG_INFO, "x86_64 GDT installed");
    xbks_log_write(XBKS_LOG_WARN, "IDT/PIC/APIC are interface stubs in this stage");
    xbks_syscall_init();
}

void xbks_arch_late_init(void) {
    xbks_log_write(XBKS_LOG_INFO, "x86_64 late architecture init");
}
