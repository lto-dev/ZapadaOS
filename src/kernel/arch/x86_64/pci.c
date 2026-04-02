/*
 * Zapada - src/kernel/arch/x86_64/pci.c
 *
 * x86-64 PCI configuration space access via port I/O.
 *
 * This is permanent C-layer code: raw port I/O stays here and nowhere else.
 * All callers above this layer see only typed C function calls.
 */

#include <kernel/arch/x86_64/pci.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/types.h>

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/*
 * make_cfg_addr - Build the 32-bit address register value for PCI config space.
 * Bit 31 set = enable; bits [23:16] = bus; bits [15:11] = device;
 * bits [10:8] = function; bits [7:2] = register (DWORD index); bits [1:0] = 0.
 */
static uint32_t make_cfg_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
    return (1u << 31)
         | ((uint32_t)bus << 16)
         | ((uint32_t)(dev & 0x1Fu) << 11)
         | ((uint32_t)(fn  & 0x07u) << 8)
         | ((uint32_t)(reg & 0xFCu));
}

static uint8_t pci_cfg_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
    uint32_t dword;
    dword = pci_cfg_read32(bus, dev, fn, (uint8_t)(reg & 0xFCu));
    return (uint8_t)((dword >> ((reg & 3u) * 8u)) & 0xFFu);
}

static uint64_t pci_bar_read_mmio_base(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t bar_index)
{
    uint8_t  reg;
    uint32_t low;
    uint32_t high;

    if (bar_index >= 6u) {
        return 0u;
    }

    reg = (uint8_t)(0x10u + (bar_index * 4u));
    low = pci_cfg_read32(bus, dev, fn, reg);
    if ((low & 1u) != 0u) {
        return 0u;
    }

    if (((low >> 1) & 3u) == 2u && bar_index < 5u) {
        high = pci_cfg_read32(bus, dev, fn, (uint8_t)(reg + 4u));
        return ((uint64_t)high << 32) | (uint64_t)(low & 0xFFFFFFF0u);
    }

    return (uint64_t)(low & 0xFFFFFFF0u);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
    outl(PCI_CFG_ADDR_PORT, make_cfg_addr(bus, dev, fn, reg));
    return inl(PCI_CFG_DATA_PORT);
}

uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg)
{
    uint32_t dword;
    outl(PCI_CFG_ADDR_PORT, make_cfg_addr(bus, dev, fn, reg & 0xFCu));
    dword = inl(PCI_CFG_DATA_PORT);
    /* Select the correct 16-bit half */
    if (reg & 2u) {
        return (uint16_t)(dword >> 16);
    }
    return (uint16_t)(dword & 0xFFFFu);
}

void pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg,
                     uint32_t value)
{
    outl(PCI_CFG_ADDR_PORT, make_cfg_addr(bus, dev, fn, reg));
    outl(PCI_CFG_DATA_PORT, value);
}

/*
 * pci_virtio_blk_probe - Scan bus 0 for a VirtIO block device.
 *
 * Accepts both legacy (0x1001) and modern (0x1042) device IDs.
 * Sets *bar0_out to the raw BAR0 config-space value (caller decodes type bits).
 */
int pci_virtio_blk_probe(uint32_t *bar0_out)
{
    uint8_t dev;

    for (dev = 0u; dev < 32u; dev++) {
        uint32_t id_reg;
        uint16_t vendor;
        uint16_t device;

        id_reg = pci_cfg_read32(0, dev, 0, 0x00u);
        /* Empty slot returns 0xFFFFFFFF */
        if (id_reg == 0xFFFFFFFFu) {
            continue;
        }

        vendor = (uint16_t)(id_reg & 0xFFFFu);
        device = (uint16_t)((id_reg >> 16) & 0xFFFFu);

        if (vendor != PCI_VENDOR_VIRTIO) {
            continue;
        }
        if (device != PCI_DEVICE_VIRTIO_BLK_LEGACY &&
            device != PCI_DEVICE_VIRTIO_BLK_MODERN) {
            continue;
        }

        /* Found a VirtIO block device — read BAR0 (config offset 0x10) */
        *bar0_out = pci_cfg_read32(0, dev, 0, 0x10u);
        return 0;
    }

    return -1;
}

int pci_virtio_blk_probe_modern(uint64_t *common_cfg_out,
                                uint64_t *notify_cfg_out,
                                uint64_t *device_cfg_out,
                                uint32_t *notify_off_multiplier_out)
{
    uint8_t dev;

    if (common_cfg_out == (uint64_t *)0 || notify_cfg_out == (uint64_t *)0 ||
        device_cfg_out == (uint64_t *)0 ||
        notify_off_multiplier_out == (uint32_t *)0) {
        return -1;
    }

    for (dev = 0u; dev < 32u; dev++) {
        uint32_t id_reg;
        uint16_t vendor;
        uint16_t device_id;
        uint16_t status;
        uint8_t  cap_ptr;

        id_reg = pci_cfg_read32(0u, dev, 0u, 0x00u);
        if (id_reg == 0xFFFFFFFFu) {
            continue;
        }

        vendor = (uint16_t)(id_reg & 0xFFFFu);
        device_id = (uint16_t)((id_reg >> 16) & 0xFFFFu);
        if (vendor != PCI_VENDOR_VIRTIO || device_id != PCI_DEVICE_VIRTIO_BLK_MODERN) {
            continue;
        }

        status = pci_cfg_read16(0u, dev, 0u, 0x06u);
        if ((status & 0x10u) == 0u) {
            continue;
        }

        cap_ptr = pci_cfg_read8(0u, dev, 0u, 0x34u);
        while (cap_ptr >= 0x40u) {
            uint8_t cap_id;
            uint8_t next;
            uint8_t cfg_type;
            uint8_t bar;
            uint32_t offset;
            uint64_t bar_base;

            cap_id = pci_cfg_read8(0u, dev, 0u, cap_ptr + 0u);
            next   = pci_cfg_read8(0u, dev, 0u, cap_ptr + 1u);
            if (cap_id == 0x09u) {
                cfg_type = pci_cfg_read8(0u, dev, 0u, cap_ptr + 3u);
                bar      = pci_cfg_read8(0u, dev, 0u, cap_ptr + 4u);
                offset   = pci_cfg_read32(0u, dev, 0u, (uint8_t)(cap_ptr + 8u));
                bar_base = pci_bar_read_mmio_base(0u, dev, 0u, bar);
                if (bar_base != 0u) {
                    if (cfg_type == 1u) {
                        *common_cfg_out = bar_base + (uint64_t)offset;
                    } else if (cfg_type == 2u) {
                        *notify_cfg_out = bar_base + (uint64_t)offset;
                        *notify_off_multiplier_out = pci_cfg_read32(0u, dev, 0u, (uint8_t)(cap_ptr + 16u));
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

    return -1;
}

