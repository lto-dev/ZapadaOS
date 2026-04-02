/*
 * Zapada - linker.ld
 *
 * ELF linker script for the Stage 1 native kernel.
 *
 * Layout:
 *   0x100000  boot sections (Multiboot2 header + 32-bit + 64-bit boot code)
 *             .text  (kernel C code)
 *             .rodata
 *             .data
 *             .bss   (includes page tables and initial stack)
 *
 * Stage 1.1 uses identity mapping (virtual address == physical address).
 * The kernel is loaded and linked at physical 0x100000 (1MB mark).
 * GRUB Multiboot2 loads the ELF at the LMA addresses defined by program headers.
 *
 * A higher-half mapping (virtual 0xFFFFFFFF80000000) is planned for a later
 * stage once the page table infrastructure is mature.
 */

ENTRY(_start)

SECTIONS
{
    /*
     * Start at 1MB (0x100000). This is above the BIOS and conventional memory
     * regions, and is the typical Multiboot2 load point.
     */
    . = 0x100000;

    /*
     * Boot sections: Multiboot2 header MUST be within the first 32KB of the
     * kernel image. Place these first to guarantee that requirement.
     */
    .multiboot2 ALIGN(8) : {
        *(.multiboot2)
    }

    .boot.text ALIGN(4) : {
        *(.boot.text)
    }

    .boot.data ALIGN(8) : {
        *(.boot.data)
    }

    /*
     * Main kernel text section (64-bit code).
     */
    .text ALIGN(4096) : {
        *(.text)
        *(.text.*)
    }

    /*
     * Read-only data.
     */
    .rodata ALIGN(4096) : {
        *(.rodata)
        *(.rodata.*)
    }

    /*
     * Initialized read-write data.
     */
    .data ALIGN(4096) : {
        *(.data)
        *(.data.*)
    }

    /*
     * Zero-initialized data: page tables, stack, and all BSS.
     * The bootloader is not required to zero BSS; the boot code does it.
     */
    .bss ALIGN(4096) : {
        bss_start = .;
        *(.boot.bss)
        *(COMMON)
        *(.bss)
        *(.bss.*)
        bss_end = .;
    }

    /*
     * Marker for the end of the kernel image.
     * Used by the frame allocator to know where kernel memory ends.
     */
    kernel_end = .;

    /*
     * Discard debug and comment sections from the final image.
     */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}

