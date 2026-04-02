/*
 * Zapada - linker_aarch64_virt.ld
 *
 * AArch64 linker script for QEMU virt.
 *
 * QEMU's generic ARM virt machine places RAM at 0x40000000, but the built-in
 * loader stub jumps into the kernel image at 0x40080000 when booting with
 * -kernel. Linking at that handoff address keeps early absolute references
 * valid before any MMU or relocation stage exists.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x40080000;

    .text.boot : {
        *(.text.boot)
    }

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

    . = ALIGN(16);
    __bss_start = .;

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    . = ALIGN(16);
    __bss_end = .;

    __kernel_end = .;
    kernel_end = __kernel_end;
}

