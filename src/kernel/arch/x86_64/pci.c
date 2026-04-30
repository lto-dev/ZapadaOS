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
#include <kernel/serial.h>
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

static int pci_device_exists(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t *id_out)
{
    uint32_t id_reg;

    id_reg = pci_cfg_read32(bus, dev, fn, 0x00u);
    if (id_reg == 0xFFFFFFFFu || id_reg == 0u) {
        return 0;
    }

    if (id_out != (uint32_t *)0) {
        *id_out = id_reg;
    }
    return 1;
}

static int pci_function_limit(uint8_t bus, uint8_t dev)
{
    uint32_t id_reg;
    uint8_t header_type;

    if (!pci_device_exists(bus, dev, 0u, &id_reg)) {
        return 0;
    }

    header_type = pci_cfg_read8(bus, dev, 0u, 0x0Eu);
    if ((header_type & 0x80u) != 0u) {
        return 8;
    }
    return 1;
}

static void pci_write_hex4(uint16_t value)
{
    static const char hex_digits[] = "0123456789ABCDEF";

    serial_write_char(hex_digits[(value >> 12) & 0xFu]);
    serial_write_char(hex_digits[(value >> 8) & 0xFu]);
    serial_write_char(hex_digits[(value >> 4) & 0xFu]);
    serial_write_char(hex_digits[value & 0xFu]);
}

static void pci_write_hex2(uint8_t value)
{
    static const char hex_digits[] = "0123456789ABCDEF";

    serial_write_char(hex_digits[(value >> 4) & 0xFu]);
    serial_write_char(hex_digits[value & 0xFu]);
}

static void pci_write_bdf(uint8_t bus, uint8_t dev, uint8_t fn)
{
    pci_write_hex2(bus);
    serial_write_char(':');
    pci_write_hex2(dev);
    serial_write_char('.');
    serial_write_dec((uint64_t)fn);
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

void pci_enable_device_io_memory_busmaster(uint8_t bus, uint8_t dev, uint8_t fn)
{
    uint32_t command_status;

    command_status = pci_cfg_read32(bus, dev, fn, 0x04u);
    command_status |= 0x00000007u;
    pci_cfg_write32(bus, dev, fn, 0x04u, command_status);
}

void pci_dump_inventory(void)
{
    uint32_t count;
    uint16_t bus;

    count = 0u;
    serial_write("PCI inventory    : scanning configuration space\n");

    for (bus = 0u; bus < 256u; bus++) {
        uint8_t dev;

        for (dev = 0u; dev < 32u; dev++) {
            int fn_limit;
            uint8_t fn;

            fn_limit = pci_function_limit((uint8_t)bus, dev);
            for (fn = 0u; fn < (uint8_t)fn_limit; fn++) {
                uint32_t id_reg;
                uint32_t class_reg;
                uint16_t vendor;
                uint16_t device;
                uint8_t class_code;
                uint8_t subclass;
                uint8_t prog_if;

                if (!pci_device_exists((uint8_t)bus, dev, fn, &id_reg)) {
                    continue;
                }

                class_reg = pci_cfg_read32((uint8_t)bus, dev, fn, 0x08u);
                vendor = (uint16_t)(id_reg & 0xFFFFu);
                device = (uint16_t)((id_reg >> 16) & 0xFFFFu);
                prog_if = (uint8_t)((class_reg >> 8) & 0xFFu);
                subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
                class_code = (uint8_t)((class_reg >> 24) & 0xFFu);

                serial_write("  pci ");
                pci_write_bdf((uint8_t)bus, dev, fn);
                serial_write(" vendor=0x");
                pci_write_hex4(vendor);
                serial_write(" device=0x");
                pci_write_hex4(device);
                serial_write(" class=0x");
                pci_write_hex2(class_code);
                pci_write_hex2(subclass);
                pci_write_hex2(prog_if);
                if (vendor == PCI_VENDOR_VIRTIO) {
                    serial_write(" virtio");
                    if (device == PCI_DEVICE_VIRTIO_BLK_LEGACY ||
                        device == PCI_DEVICE_VIRTIO_BLK_MODERN) {
                        serial_write(" block");
                    }
                }
                serial_write("\n");
                count++;
            }
        }
    }

    serial_write("PCI inventory    : ");
    serial_write_dec((uint64_t)count);
    serial_write(" device functions\n");
}

/*
 * pci_virtio_blk_probe - Scan PCI configuration space for a VirtIO block device.
 *
 * Accepts both legacy (0x1001) and modern (0x1042) device IDs.
 * Sets *bar0_out to the raw BAR0 config-space value (caller decodes type bits).
 */
int pci_virtio_blk_probe_nth(uint32_t target_index, uint32_t *bar0_out)
{
    uint16_t bus;
    uint32_t matched;

    matched = 0u;

    for (bus = 0u; bus < 256u; bus++) {
        uint8_t dev;

        for (dev = 0u; dev < 32u; dev++) {
            int fn_limit;
            uint8_t fn;

            fn_limit = pci_function_limit((uint8_t)bus, dev);
            for (fn = 0u; fn < (uint8_t)fn_limit; fn++) {
                uint32_t id_reg;
                uint16_t vendor;
                uint16_t device;

                if (!pci_device_exists((uint8_t)bus, dev, fn, &id_reg)) {
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

                if (matched == target_index) {
                    pci_enable_device_io_memory_busmaster((uint8_t)bus, dev, fn);
                    *bar0_out = pci_cfg_read32((uint8_t)bus, dev, fn, 0x10u);
                    return 0;
                }

                matched++;
            }
        }
    }

    return -1;
}

int pci_virtio_blk_probe(uint32_t *bar0_out)
{
    return pci_virtio_blk_probe_nth(0u, bar0_out);
}

int pci_virtio_blk_probe_modern(uint64_t *common_cfg_out,
                                uint64_t *notify_cfg_out,
                                uint64_t *device_cfg_out,
                                uint32_t *notify_off_multiplier_out)
{
    uint16_t bus;

    if (common_cfg_out == (uint64_t *)0 || notify_cfg_out == (uint64_t *)0 ||
        device_cfg_out == (uint64_t *)0 ||
        notify_off_multiplier_out == (uint32_t *)0) {
        return -1;
    }

    for (bus = 0u; bus < 256u; bus++) {
        uint8_t dev;

        for (dev = 0u; dev < 32u; dev++) {
            int fn_limit;
            uint8_t fn;

            fn_limit = pci_function_limit((uint8_t)bus, dev);
            for (fn = 0u; fn < (uint8_t)fn_limit; fn++) {
                uint32_t id_reg;
                uint16_t vendor;
                uint16_t device_id;
                uint16_t status;
                uint8_t cap_ptr;

                if (!pci_device_exists((uint8_t)bus, dev, fn, &id_reg)) {
                    continue;
                }

                vendor = (uint16_t)(id_reg & 0xFFFFu);
                device_id = (uint16_t)((id_reg >> 16) & 0xFFFFu);
                if (vendor != PCI_VENDOR_VIRTIO || device_id != PCI_DEVICE_VIRTIO_BLK_MODERN) {
                    continue;
                }

                pci_enable_device_io_memory_busmaster((uint8_t)bus, dev, fn);

                status = pci_cfg_read16((uint8_t)bus, dev, fn, 0x06u);
                if ((status & 0x10u) == 0u) {
                    continue;
                }

                cap_ptr = pci_cfg_read8((uint8_t)bus, dev, fn, 0x34u);
                while (cap_ptr >= 0x40u) {
                    uint8_t cap_id;
                    uint8_t next;
                    uint8_t cfg_type;
                    uint8_t bar;
                    uint32_t offset;
                    uint64_t bar_base;

                    cap_id = pci_cfg_read8((uint8_t)bus, dev, fn, cap_ptr + 0u);
                    next   = pci_cfg_read8((uint8_t)bus, dev, fn, cap_ptr + 1u);
                    if (cap_id == 0x09u) {
                        cfg_type = pci_cfg_read8((uint8_t)bus, dev, fn, cap_ptr + 3u);
                        bar      = pci_cfg_read8((uint8_t)bus, dev, fn, cap_ptr + 4u);
                        offset   = pci_cfg_read32((uint8_t)bus, dev, fn, (uint8_t)(cap_ptr + 8u));
                        bar_base = pci_bar_read_mmio_base((uint8_t)bus, dev, fn, bar);
                        if (bar_base != 0u) {
                            if (cfg_type == 1u) {
                                *common_cfg_out = bar_base + (uint64_t)offset;
                            } else if (cfg_type == 2u) {
                                *notify_cfg_out = bar_base + (uint64_t)offset;
                                *notify_off_multiplier_out = pci_cfg_read32((uint8_t)bus, dev, fn, (uint8_t)(cap_ptr + 16u));
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
    }

    return -1;
}

