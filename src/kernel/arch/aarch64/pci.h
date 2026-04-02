/*
 * Zapada - src/kernel/arch/aarch64/pci.h
 *
 * Minimal PCIe ECAM scanner for AArch64 platforms that expose a generic
 * `pci-host-ecam-generic` node, such as QEMU virt and Raspberry Pi 4/CM4.
 */

#ifndef ZAPADA_ARCH_AARCH64_PCI_H
#define ZAPADA_ARCH_AARCH64_PCI_H

#include <kernel/types.h>

#define AA64_PCI_VENDOR_VIRTIO             0x1AF4u
#define AA64_PCI_DEVICE_VIRTIO_BLK_LEGACY  0x1001u
#define AA64_PCI_DEVICE_VIRTIO_BLK_MODERN  0x1042u

/* QEMU virt generic ECAM host bridge default. */
#define AA64_QEMU_VIRT_PCIE_ECAM_BASE      0x10000000UL

typedef struct {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  fn;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar0;
    uint32_t status;
    uint8_t  cap_ptr;
} aa64_pci_device_t;

uint32_t aa64_pci_cfg_read32(uint64_t ecam_base, uint8_t bus, uint8_t dev,
                             uint8_t fn, uint16_t reg);
uint16_t aa64_pci_cfg_read16(uint64_t ecam_base, uint8_t bus, uint8_t dev,
                             uint8_t fn, uint16_t reg);
int aa64_pci_virtio_blk_probe(uint64_t ecam_base, aa64_pci_device_t *out_dev);
int aa64_pci_virtio_blk_probe_modern(uint64_t ecam_base,
                                     uint64_t *common_cfg_out,
                                     uint64_t *notify_cfg_out,
                                     uint64_t *device_cfg_out,
                                     uint32_t *notify_off_multiplier_out);

#endif /* ZAPADA_ARCH_AARCH64_PCI_H */


