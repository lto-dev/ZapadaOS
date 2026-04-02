/*
 * Zapada - src/kernel/arch/x86_64/gdt.c
 *
 * GDT setup for Phase 2C: 5 entries.
 *
 * Index  Selector  DPL  Description
 *   0    0x00      -    null descriptor
 *   1    0x08      0    kernel 64-bit code (ring 0)
 *   2    0x10      0    kernel data (ring 0)
 *   3    0x1B      3    user 64-bit code (ring 3)  selector = 3*8 | RPL3
 *   4    0x23      3    user data (ring 3)          selector = 4*8 | RPL3
 *
 * Access byte encoding (Phase 2C):
 *   0x9A = 1001 1010b : P=1, DPL=0, S=1, Type=A (code, exec, readable)
 *   0x92 = 1001 0010b : P=1, DPL=0, S=1, Type=2 (data, writable)
 *   0xFA = 1111 1010b : P=1, DPL=3, S=1, Type=A (code ring-3, exec, readable)
 *   0xF2 = 1111 0010b : P=1, DPL=3, S=1, Type=2 (data ring-3, writable)
 *
 * Granularity byte for 64-bit code: 0xA0 (G=1, L=1 sets 64-bit).
 * Granularity byte for data:        0xC0 (G=1, B=1 for 32/64-bit data).
 */

#include <kernel/arch/x86_64/gdt.h>

typedef struct gdt_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_descriptor_t;

typedef struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdtr_t;

/* 5 entries: null + kernel code + kernel data + user code + user data */
static gdt_descriptor_t g_gdt[5];
static gdtr_t           g_gdtr;

static void gdt_set_descriptor(uint32_t index,
                                uint32_t base,
                                uint32_t limit,
                                uint8_t access,
                                uint8_t granularity)
{
    gdt_descriptor_t *descriptor = &g_gdt[index];

    descriptor->limit_low   = (uint16_t)(limit & 0xFFFFu);
    descriptor->base_low    = (uint16_t)(base & 0xFFFFu);
    descriptor->base_middle = (uint8_t)((base >> 16) & 0xFFu);
    descriptor->access      = access;
    descriptor->granularity = (uint8_t)(((limit >> 16) & 0x0Fu)
                              | (granularity & 0xF0u));
    descriptor->base_high   = (uint8_t)((base >> 24) & 0xFFu);
}

void gdt_init(void)
{
    /* 0: null */
    gdt_set_descriptor(0, 0, 0, 0, 0);
    /* 1: kernel code - 64-bit, DPL=0, exec+readable */
    gdt_set_descriptor(1, 0, 0x000FFFFFu, 0x9Au, 0xA0u);
    /* 2: kernel data - DPL=0, writable */
    gdt_set_descriptor(2, 0, 0x000FFFFFu, 0x92u, 0xC0u);
    /* 3: user code - 64-bit, DPL=3, exec+readable */
    gdt_set_descriptor(3, 0, 0x000FFFFFu, 0xFAu, 0xA0u);
    /* 4: user data - DPL=3, writable */
    gdt_set_descriptor(4, 0, 0x000FFFFFu, 0xF2u, 0xC0u);

    g_gdtr.limit = (uint16_t)(sizeof(g_gdt) - 1);
    g_gdtr.base  = (uint64_t)(uintptr_t)&g_gdt[0];

    __asm__ volatile (
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        :
        : "m"(g_gdtr)
        : "rax", "memory"
    );
}

