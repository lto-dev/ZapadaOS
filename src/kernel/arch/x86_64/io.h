/*
 * Zapada - src/kernel/arch/x86_64/io.h
 *
 * x86-64 port I/O primitives.
 *
 * All direct port I/O in the kernel must go through these helpers.
 * No other translation unit may embed inline `inb`/`outb` assembly directly.
 * This enforces the unsafe-boundary rule: unsafe access lives only under
 * src/kernel/arch/.
 *
 * All functions are static inline to avoid a separate translation unit while
 * keeping the unsafe boundary visible at the include site.
 */

#ifndef ZAPADA_ARCH_X86_64_IO_H
#define ZAPADA_ARCH_X86_64_IO_H

#include <kernel/types.h>

/*
 * outb - write a byte to an I/O port.
 */
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

/*
 * inb - read a byte from an I/O port.
 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/*
 * outw - write a 16-bit word to an I/O port.
 */
static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

/*
 * inw - read a 16-bit word from an I/O port.
 */
static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/*
 * outl - write a 32-bit dword to an I/O port.
 * Used for PCI config space address register (0xCF8).
 */
static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

/*
 * inl - read a 32-bit dword from an I/O port.
 * Used for PCI config space data register (0xCFC).
 */
static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

#endif /* ZAPADA_ARCH_X86_64_IO_H */


