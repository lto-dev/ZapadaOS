/*
 * Zapada - src/kernel/arch/aarch64/pci.c
 *
 * AArch64 PCIe ECAM configuration space access.
 *
 * This is the AArch64 equivalent of the x86_64 PCI config scanner, except the
 * transport is MMIO through an ECAM window rather than port I/O.
 */

#include <kernel/arch/aarch64/pci.h>
#include <kernel/types.h>

static uint8_t aa64_pci_cfg_read8(uint64_t ecam_base, uint8_t bus, uint8_t dev,
                                  uint8_t fn, uint16_t reg)
{
    uint32_t dword;

    dword = aa64_pci_cfg_read32(ecam_base, bus, dev, fn, (uint16_t)(reg & 0xFFCu));
    return (uint8_t)((dword >> ((reg & 3u) * 8u)) & 0xFFu);
}

static uint64_t aa64_pci_bar_read_mmio_base(uint64_t ecam_base, uint8_t bus,
                                            uint8_t dev, uint8_t fn,
                                            uint8_t bar_index)
{
    uint16_t reg;
    uint32_t low;
    uint32_t high;

    if (bar_index >= 6u) {
        return 0u;
    }

    reg = (uint16_t)(0x10u + (bar_index * 4u));
    low = aa64_pci_cfg_read32(ecam_base, bus, dev, fn, reg);
    if ((low & 1u) != 0u) {
        return 0u;
    }

    if (((low >> 1) & 3u) == 2u && bar_index < 5u) {
        high = aa64_pci_cfg_read32(ecam_base, bus, dev, fn, (uint16_t)(reg + 4u));
        return ((uint64_t)high << 32) | (uint64_t)(low & 0xFFFFFFF0u);
    }

    return (uint64_t)(low & 0xFFFFFFF0u);
}

static uint64_t aa64_pci_cfg_addr(uint64_t ecam_base, uint8_t bus, uint8_t dev,
                                  uint8_t fn, uint16_t reg)
{
    return ecam_base
         + ((uint64_t)bus << 20)
         + ((uint64_t)(dev & 0x1Fu) << 15)
         + ((uint64_t)(fn & 0x07u) << 12)
         + (uint64_t)(reg & 0x0FFFu);
}

uint32_t aa64_pci_cfg_read32(uint64_t ecam_base, uint8_t bus, uint8_t dev,
                             uint8_t fn, uint16_t reg)
{
    volatile uint32_t *cfg;

    cfg = (volatile uint32_t *)(uintptr_t)aa64_pci_cfg_addr(ecam_base, bus, dev,
                                                            fn, (uint16_t)(reg & 0xFFCu));
    return *cfg;
}

uint16_t aa64_pci_cfg_read16(uint64_t ecam_base, uint8_t bus, uint8_t dev,
                             uint8_t fn, uint16_t reg)
{
    uint32_t dword;

    dword = aa64_pci_cfg_read32(ecam_base, bus, dev, fn, (uint16_t)(reg & 0xFFCu));
    if ((reg & 2u) != 0u) {
        return (uint16_t)(dword >> 16);
    }
    return (uint16_t)(dword & 0xFFFFu);
}

int aa64_pci_virtio_blk_probe(uint64_t ecam_base, aa64_pci_device_t *out_dev)
{
    uint16_t bus;
    uint8_t dev;

    if (out_dev == (aa64_pci_device_t *)0) {
        return -1;
    }

    for (bus = 0u; bus < 256u; bus++) {
        for (dev = 0u; dev < 32u; dev++) {
            uint32_t id_reg;
            uint16_t vendor;
            uint16_t device;

            id_reg = aa64_pci_cfg_read32(ecam_base, (uint8_t)bus, dev, 0u, 0x000u);
            if (id_reg == 0xFFFFFFFFu) {
                continue;
            }

            vendor = (uint16_t)(id_reg & 0xFFFFu);
            device = (uint16_t)((id_reg >> 16) & 0xFFFFu);

            if (vendor != AA64_PCI_VENDOR_VIRTIO) {
                continue;
            }
            if (device != AA64_PCI_DEVICE_VIRTIO_BLK_LEGACY &&
                device != AA64_PCI_DEVICE_VIRTIO_BLK_MODERN) {
                continue;
            }

            out_dev->bus       = (uint8_t)bus;
            out_dev->dev       = dev;
            out_dev->fn        = 0u;
            out_dev->vendor_id = vendor;
            out_dev->device_id = device;
            out_dev->bar0      = aa64_pci_cfg_read32(ecam_base, (uint8_t)bus, dev, 0u, 0x010u);
            out_dev->status    = aa64_pci_cfg_read16(ecam_base, (uint8_t)bus, dev, 0u, 0x006u);
            out_dev->cap_ptr   = (uint8_t)(aa64_pci_cfg_read16(ecam_base, (uint8_t)bus, dev, 0u, 0x034u) & 0x00FFu);
            return 0;
        }
    }

    return -1;
}

int aa64_pci_virtio_blk_probe_modern(uint64_t ecam_base,
                                     uint64_t *common_cfg_out,
                                     uint64_t *notify_cfg_out,
                                     uint64_t *device_cfg_out,
                                     uint32_t *notify_off_multiplier_out)
{
    uint16_t bus;
    uint8_t dev;

    if (common_cfg_out == (uint64_t *)0 || notify_cfg_out == (uint64_t *)0 ||
        device_cfg_out == (uint64_t *)0 ||
        notify_off_multiplier_out == (uint32_t *)0) {
        return -1;
    }

    for (bus = 0u; bus < 256u; bus++) {
        for (dev = 0u; dev < 32u; dev++) {
            uint32_t id_reg;
            uint16_t vendor;
            uint16_t device_id;
            uint16_t status;
            uint8_t  cap_ptr;

            id_reg = aa64_pci_cfg_read32(ecam_base, (uint8_t)bus, dev, 0u, 0x000u);
            if (id_reg == 0xFFFFFFFFu) {
                continue;
            }

            vendor = (uint16_t)(id_reg & 0xFFFFu);
            device_id = (uint16_t)((id_reg >> 16) & 0xFFFFu);
            if (vendor != AA64_PCI_VENDOR_VIRTIO ||
                device_id != AA64_PCI_DEVICE_VIRTIO_BLK_MODERN) {
                continue;
            }

            status = aa64_pci_cfg_read16(ecam_base, (uint8_t)bus, dev, 0u, 0x006u);
            if ((status & 0x10u) == 0u) {
                continue;
            }

            cap_ptr = aa64_pci_cfg_read8(ecam_base, (uint8_t)bus, dev, 0u, 0x034u);
            while (cap_ptr >= 0x40u) {
                uint8_t cap_id;
                uint8_t next;
                uint8_t cfg_type;
                uint8_t bar;
                uint32_t offset;
                uint64_t bar_base;

                cap_id = aa64_pci_cfg_read8(ecam_base, (uint8_t)bus, dev, 0u, (uint16_t)(cap_ptr + 0u));
                next   = aa64_pci_cfg_read8(ecam_base, (uint8_t)bus, dev, 0u, (uint16_t)(cap_ptr + 1u));
                if (cap_id == 0x09u) {
                    cfg_type = aa64_pci_cfg_read8(ecam_base, (uint8_t)bus, dev, 0u, (uint16_t)(cap_ptr + 3u));
                    bar      = aa64_pci_cfg_read8(ecam_base, (uint8_t)bus, dev, 0u, (uint16_t)(cap_ptr + 4u));
                    offset   = aa64_pci_cfg_read32(ecam_base, (uint8_t)bus, dev, 0u, (uint16_t)(cap_ptr + 8u));
                    bar_base = aa64_pci_bar_read_mmio_base(ecam_base, (uint8_t)bus, dev, 0u, bar);
                    if (bar_base != 0u) {
                        if (cfg_type == 1u) {
                            *common_cfg_out = bar_base + (uint64_t)offset;
                        } else if (cfg_type == 2u) {
                            *notify_cfg_out = bar_base + (uint64_t)offset;
                            *notify_off_multiplier_out = aa64_pci_cfg_read32(ecam_base, (uint8_t)bus, dev, 0u, (uint16_t)(cap_ptr + 16u));
                        } else if (cfg_type == 4u) {
                            *device_cfg_out = bar_base + (uint64_t)offset;
                        }
                    }
                }

                if (next == 0u || next == cap_ptr) {
                    break;
                }
                cap_ptr = next;
            }

            if (*common_cfg_out != 0u && *notify_cfg_out != 0u && *device_cfg_out != 0u) {
                return 0;
            }
        }
    }

    return -1;
}

