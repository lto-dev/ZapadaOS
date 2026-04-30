/*
 * Zapada - src/kernel/arch/aarch64/pci.h
 *
 * Minimal PCIe ECAM scanner for AArch64 platforms that expose a generic
 * `pci-host-ecam-generic` node, such as QEMU virt and Raspberry Pi 4/CM4.
 */

#ifndef ZAPADA_ARCH_AARCH64_PCI_H
#define ZAPADA_ARCH_AARCH64_PCI_H

#include <kernel/types.h>

/* QEMU virt generic ECAM host bridge default. */
#define AA64_QEMU_VIRT_PCIE_ECAM_BASE      0x10000000UL

uint32_t aa64_pci_cfg_read32(uint64_t ecam_base, uint8_t bus, uint8_t dev,
                             uint8_t fn, uint16_t reg);
uint16_t aa64_pci_cfg_read16(uint64_t ecam_base, uint8_t bus, uint8_t dev,
                             uint8_t fn, uint16_t reg);

#endif /* ZAPADA_ARCH_AARCH64_PCI_H */


