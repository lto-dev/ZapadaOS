/*
 * Zapada - linker_aarch64.ld
 *
 * AArch64 linker script for Raspberry Pi 3/4/5.
 *
 * The RPi bootloader and QEMU (-M raspi3b) load kernel8.img to physical
 * address 0x80000 and jump to the entry point (_start).
 *
 * Memory layout:
 *   0x80000 : .text.boot (entry point _start from boot.S)
 *   ...     : .text (kernel code)
 *   ...     : .rodata (read-only data)
 *   ...     : .data (initialized data)
 *   ...     : .bss (zero-initialized; cleared by boot.S)
 *
 * BSS symbols are exported so boot.S can clear the region before
 * calling kernel_main_aarch64.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x80000;

    /* Boot entry point must be the very first section. */
    .text.boot : {
        *(.text.boot)
    }

    /*
     * Exception vector table. VBAR_EL1 requires 2 KiB (0x800) alignment.
     * Placed before the general .text block so the linker honours the
     * 2048-byte alignment without displacing code pages.
     */
    . = ALIGN(2048);
    .text.exception_vectors : {
        *(.text.exception_vectors)
    }

    .text : {
        . = ALIGN(4096);
        *(.text*)
    }

    .rodata : {
        . = ALIGN(4096);
        *(.rodata*)
    }

    .data : {
        . = ALIGN(4096);
        *(.data*)
    }

    . = ALIGN(8);
    __bss_start = .;

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    . = ALIGN(8);
    __bss_end = .;

    __kernel_end = .;

    /*
     * kernel_end - alias matching the symbol name declared in mm_defs.h.
     * The PMM and heap use (uintptr_t)&kernel_end to find the first byte
     * after the kernel image. Both symbols point to the same address.
     */
    kernel_end = __kernel_end;
}

