/*
 * Zapada - src/kernel/arch/x86_64/pci.h
 *
 * x86-64 PCI configuration space scanner.
 *
 * Permanent C layer: uses port I/O (outl 0xCF8 / inl 0xCFC) to access PCI
 * configuration space.  All raw port access is confined to this translation
 * unit; nothing above this layer may call outl/inl directly.
 *
 * The active managed driver path consumes this file only as a generic PCI
 * configuration-space primitive provider.
 */

#ifndef ZAPADA_ARCH_X86_64_PCI_H
#define ZAPADA_ARCH_X86_64_PCI_H

#include <kernel/types.h>

/* PCI configuration space port addresses */
#define PCI_CFG_ADDR_PORT  0x0CF8u
#define PCI_CFG_DATA_PORT  0x0CFCu

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

#endif /* ZAPADA_ARCH_X86_64_PCI_H */


