/*
 * Zapada - src/kernel/arch/x86_64/pci.h
 *
 * x86-64 PCI configuration space scanner.
 *
 * Permanent C layer: uses port I/O (outl 0xCF8 / inl 0xCFC) to access PCI
 * configuration space.  All raw port access is confined to this translation
 * unit; nothing above this layer may call outl/inl directly.
 *
 * Phase 3A: scans PCI configuration space for any VirtIO block device
 * (vendor 0x1AF4, device 0x1001 legacy or 0x1042 modern) and returns the
 * BAR0 base.
 */

#ifndef ZAPADA_ARCH_X86_64_PCI_H
#define ZAPADA_ARCH_X86_64_PCI_H

#include <kernel/types.h>

/* PCI configuration space port addresses */
#define PCI_CFG_ADDR_PORT  0x0CF8u
#define PCI_CFG_DATA_PORT  0x0CFCu

/* VirtIO vendor / device IDs */
#define PCI_VENDOR_VIRTIO             0x1AF4u
#define PCI_DEVICE_VIRTIO_BLK_LEGACY  0x1001u   /* transitional (legacy) */
#define PCI_DEVICE_VIRTIO_BLK_MODERN  0x1042u   /* non-transitional (modern) */

/*
 * pci_cfg_read32 - Read a 32-bit configuration register.
 *
 * @bus  PCI bus number (0-255)
 * @dev  PCI device number (0-31)
 * @fn   PCI function number (0-7)
 * @reg  Configuration register byte offset (must be DWORD-aligned)
 *
 * Returns the 32-bit register value, or 0xFFFFFFFF if the slot is empty.
 */
uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);

/*
 * pci_cfg_read16 - Read a 16-bit configuration register.
 */
uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);

/*
 * pci_cfg_write32 - Write a 32-bit configuration register.
 */
void pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg,
                     uint32_t value);

/*
 * pci_enable_device_io_memory_busmaster - Enable I/O, MMIO, and DMA access.
 */
void pci_enable_device_io_memory_busmaster(uint8_t bus, uint8_t dev, uint8_t fn);

/*
 * pci_dump_inventory - Print Linux-style PCI discovery lines for boot logs.
 */
void pci_dump_inventory(void);

/*
 * pci_virtio_blk_probe - Scan PCI configuration space for a VirtIO block device.
 *
 * Accepts both legacy (0x1001) and modern (0x1042) VirtIO block device IDs.
 *
 * On success sets *bar0_out to the raw BAR0 value (caller must strip the
 * type bits) and returns 0.  Returns -1 if no device is found.
 *
 * Bit 0 of bar0: 0 = MMIO bar, 1 = I/O port bar.
 * For an I/O bar, strip bits [1:0]; for MMIO, strip bits [3:0].
 */
int pci_virtio_blk_probe(uint32_t *bar0_out);
int pci_virtio_blk_probe_nth(uint32_t target_index, uint32_t *bar0_out);

int pci_virtio_blk_probe_modern(uint64_t *common_cfg_out,
                                uint64_t *notify_cfg_out,
                                uint64_t *device_cfg_out,
                                uint32_t *notify_off_multiplier_out);

#endif /* ZAPADA_ARCH_X86_64_PCI_H */


