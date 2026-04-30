#ifndef ZAPADA_ARCH_AARCH64_FDT_H
#define ZAPADA_ARCH_AARCH64_FDT_H

#include <kernel/types.h>

/*
 * Minimal Flattened Device Tree (FDT/DTB) helper for AArch64.
 *
 * The RPi firmware (and QEMU) may pass a Devicetree Blob (DTB) address
 * to the kernel in x0. This module validates the FDT and extracts the
 * first usable RAM region from the /memory node.
 *
 * FDT magic: 0xD00DFEED (big-endian at offset 0 of the DTB blob).
 *
 * fdt_is_valid   - returns 1 if the given address holds a valid FDT.
 * fdt_get_memory - scan the FDT for the first /memory reg entry; writes
 *                  the region base and size to *out_base and *out_size.
 *                  Returns 1 on success, 0 if no memory node found.
 *
 * Both functions handle NULL / unaligned addresses safely.
 *
 * Fallback layout (used when no valid FDT is present):
 *   The BCM2837 family (Raspberry Pi 3, 3+, 3A+) has 1 GiB of SDRAM at
 *   physical address 0x00000000. When the FDT is absent or invalid, these
 *   constants are used. When a valid FDT is available, fdt_get_memory()
 *   returns the actual layout so real hardware differences are respected.
 */

#define FDT_MAGIC   0xD00DFEEDUL

int      fdt_is_valid(uint64_t fdt_base);
int      fdt_get_memory(uint64_t fdt_base, uint64_t *out_base, uint64_t *out_size);
int      fdt_get_initrd(uint64_t fdt_base, uint64_t *out_start, uint64_t *out_end);
const char *fdt_get_bootargs(uint64_t fdt_base);

/* Known-good fallback for BCM2837 family (RPi 3, 3+, 3A+) - 1 GiB SDRAM. */
#define RPI3_RAM_BASE  0x00000000UL
#define RPI3_RAM_SIZE  0x40000000UL   /* 1 GiB */

#endif /* ZAPADA_ARCH_AARCH64_FDT_H */

