#include <xbks/cpu.h>
#include <xbks/log.h>
#include <xbks/process.h>
#include <xbks/syscall.h>

extern void xbks_x86_64_syscall_entry(void);

enum {
    EFER_SYSCALL_ENABLE = 1u << 0,
    RFLAGS_INTERRUPT_ENABLE = 1u << 9,
    RFLAGS_DIRECTION_FLAG = 1u << 10,
};

void xbks_syscall_init(void) {
    const uint64_t efer = xbks_rdmsr(XBKS_MSR_EFER);

    xbks_wrmsr(XBKS_MSR_EFER, efer | EFER_SYSCALL_ENABLE);
    xbks_wrmsr(XBKS_MSR_LSTAR, (uint64_t)(uintptr_t)xbks_x86_64_syscall_entry);

    xbks_wrmsr(XBKS_MSR_STAR, ((uint64_t)0x08u << 32) | ((uint64_t)0x18u << 48));
    xbks_wrmsr(XBKS_MSR_FMASK, RFLAGS_INTERRUPT_ENABLE | RFLAGS_DIRECTION_FLAG);
    xbks_log_write(XBKS_LOG_INFO, "x86_64 syscall MSRs configured");
}

uint64_t xbks_syscall_dispatch(struct xbks_syscall_frame *frame) {
    return xbks_process_syscall_dispatch(frame);
}
