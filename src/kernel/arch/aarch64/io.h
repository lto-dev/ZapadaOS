#ifndef ZAPADA_ARCH_AARCH64_IO_H
#define ZAPADA_ARCH_AARCH64_IO_H

#include <kernel/types.h>

/*
 * Memory-mapped I/O helpers for AArch64.
 *
 * All peripheral registers on ARMv8-A are accessed via MMIO (there are no
 * IN/OUT port instructions). The DSB (Data Synchronization Barrier) ensures
 * that MMIO stores are visible to the peripheral before subsequent reads,
 * which is required for correct device operation.
 *
 * mmio_readl  - read a 32-bit peripheral register.
 * mmio_writel - write a 32-bit peripheral register.
 */

static inline uint32_t mmio_readl(uintptr_t addr)
{
    uint32_t val;
    __asm__ volatile (
        "dsb   st\n"
        "ldr   %w0, [%1]\n"
        : "=r"(val)
        : "r"(addr)
        : "memory"
    );
    return val;
}

static inline void mmio_writel(uintptr_t addr, uint32_t val)
{
    __asm__ volatile (
        "str   %w0, [%1]\n"
        "dsb   st\n"
        :
        : "r"(val), "r"(addr)
        : "memory"
    );
}

#endif /* ZAPADA_ARCH_AARCH64_IO_H */

