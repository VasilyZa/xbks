#ifndef XBKS_CPU_H
#define XBKS_CPU_H

#include <xbks/types.h>

enum {
    XBKS_MSR_EFER = 0xc0000080u,
    XBKS_MSR_STAR = 0xc0000081u,
    XBKS_MSR_LSTAR = 0xc0000082u,
    XBKS_MSR_FMASK = 0xc0000084u,
};

static inline uint64_t xbks_rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
    return ((uint64_t)high << 32) | low;
}

static inline void xbks_wrmsr(uint32_t msr, uint64_t value) {
    const uint32_t low = (uint32_t)value;
    const uint32_t high = (uint32_t)(value >> 32);

    __asm__ volatile ("wrmsr" : : "c" (msr), "a" (low), "d" (high) : "memory");
}

#endif
