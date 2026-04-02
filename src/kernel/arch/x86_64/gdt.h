#ifndef ZAPADA_ARCH_X86_64_GDT_H
#define ZAPADA_ARCH_X86_64_GDT_H

#include <kernel/types.h>

/*
 * Segment selector values (index * 8 + RPL bits).
 *
 * GDT layout (Phase 2C, 5 entries):
 *   0x00  null descriptor
 *   0x08  kernel code (ring 0, 64-bit)       CS = 0x08
 *   0x10  kernel data (ring 0)               DS = 0x10
 *   0x1B  user code   (ring 3, 64-bit)       CS = 0x18 | RPL3 = 0x1B
 *   0x23  user data   (ring 3)               DS = 0x20 | RPL3 = 0x23
 */
#define GDT_SEG_NULL        0x00u
#define GDT_SEG_KERNEL_CODE 0x08u   /* ring-0 code segment selector */
#define GDT_SEG_KERNEL_DATA 0x10u   /* ring-0 data segment selector */
#define GDT_SEG_USER_CODE   0x1Bu   /* ring-3 code segment selector (RPL=3) */
#define GDT_SEG_USER_DATA   0x23u   /* ring-3 data segment selector (RPL=3) */

void gdt_init(void);

#endif /* ZAPADA_ARCH_X86_64_GDT_H */

