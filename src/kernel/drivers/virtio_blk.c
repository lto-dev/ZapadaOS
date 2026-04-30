/*
 * Zapada - src/kernel/drivers/virtio_blk.c
 *
 * VirtIO block device probe, initialisation, and InternalCall entry points.
 *
 * Responsibilities:
 *   - Probe the hardware (PCI on x86_64, MMIO on AArch64) for a VirtIO block device
 *   - Initialise the VirtIO transport layer (virtio.c)
 *   - Populate the block_device_t descriptor for "vda"
 *   - Provide native_read_sector() and native_write_sector() as the C bodies
 *     of the Zapada.BlockDev.ReadSector / WriteSector InternalCalls
 *
 * Array object layout (Phase 3B Step B.2, CLR_ARR_HDR_SIZE = 24 from gc.h):
 *   bytes  0- 3: type_idx   = CLR_TYPE_ARRAY (GC header, offset 0)
 *   bytes  4- 7: alloc_size (GC header, offset 4)
 *   bytes  8-11: elem_type_token (CLR_ARR_OFF_ELEM_TYPE = 8)
 *   bytes 12-15: elem_size       (CLR_ARR_OFF_ELEM_SIZE = 12)
 *   bytes 16-19: element count   (CLR_ARR_OFF_COUNT = 16)
 *   bytes 20-23: padding = 0
 *   bytes 24..:  element data    (CLR_ARR_HDR_SIZE = 24)
 *
 * DMA note: kheap_alloc / pmm_alloc_frame return physically contiguous memory
 * with virtual == physical (identity mapped).  The VirtIO device uses the
 * virtual pointer directly as the DMA bus address.
 */

#include <kernel/drivers/virtio_blk.h>
#include <kernel/drivers/virtio.h>
#include <kernel/drivers/block.h>
#include <kernel/types.h>
#include <kernel/serial.h>

#define CLR_ARR_HDR_SIZE 24u

#ifdef ARCH_X86_64
#include <kernel/arch/x86_64/pci.h>
#include <kernel/arch/x86_64/io.h>

/* Legacy VirtIO PCI capacity register offset from I/O base */
#define LVIO_CAPACITY_LO  0x14u   /* 32-bit lo word of 64-bit capacity */
#define LVIO_CAPACITY_HI  0x18u   /* 32-bit hi word */
#endif

#ifdef ARCH_AARCH64
#include <kernel/arch/aarch64/pci.h>
#include <kernel/arch/aarch64/virtio_mmio.h>

/* Modern VirtIO-MMIO block device config (at base + 0x100) */
#define MVIO_BLK_CAPACITY_LO  0x100u  /* 32-bit lo */
#define MVIO_BLK_CAPACITY_HI  0x104u  /* 32-bit hi */
#endif

/*
 * CLR_ARR_HDR_SIZE is defined in <kernel/clr/gc.h> as 24u (Phase 3B Step B.2
 * layout).  The local define was removed to ensure a single source of truth.
 * Array element data starts at arr_obj + CLR_ARR_HDR_SIZE.
 */

/* --------------------------------------------------------------------------
 * Global state — BSS, no heap
 * -------------------------------------------------------------------------- */

virtio_dev_t  g_virtio_blk_dev;   /* Compatibility alias for first probe */
virtio_dev_t  g_virtio_blk_devs[BLOCK_DEV_MAX];
block_device_t g_block_vda;       /* Compatibility descriptor for first probe */
block_device_t g_block_devices[BLOCK_DEV_MAX];
uint32_t g_block_device_count;

static void virtio_blk_set_name(block_device_t *block, uint32_t index)
{
    if (block == (block_device_t *)0) {
        return;
    }

    block->name[0] = 'v';
    block->name[1] = 'd';
    block->name[2] = (char)('a' + (char)index);
    block->name[3] = '\0';
}

static const char *virtio_blk_transport_name(const virtio_dev_t *dev)
{
    if (dev == (const virtio_dev_t *)0) {
        return "unknown";
    }
    if (dev->is_legacy != 0) {
        return "virtio-pci-legacy";
    }
    if (dev->is_modern_pci != 0) {
        return "virtio-pci-modern";
    }
    return "virtio-mmio";
}

void virtio_blk_dump_inventory(void)
{
    uint32_t i;

    serial_write("Block inventory  : ");
    if (g_block_device_count == 0u) {
        serial_write("no disks\n");
        return;
    }

    for (i = 0u; i < g_block_device_count; i++) {
        if (i != 0u) {
            serial_write("                   ");
        }
        serial_write(g_block_devices[i].name);
        serial_write(" sectors=");
        serial_write_dec(g_block_devices[i].sector_count);
        serial_write(" sector_size=");
        serial_write_dec((uint64_t)g_block_devices[i].sector_size);
        serial_write(" transport=");
        serial_write(virtio_blk_transport_name(&g_virtio_blk_devs[i]));
        serial_write("\n");
    }
}

/* --------------------------------------------------------------------------
 * virtio_blk_probe_and_init
 * -------------------------------------------------------------------------- */

int virtio_blk_probe_and_init(void)
{
#ifdef ARCH_X86_64
    uint32_t found;

    found = 0u;
    while (found < BLOCK_DEV_MAX) {
        uint32_t bar0;
        uint32_t cap_lo;
        uint32_t cap_hi;
        uint16_t io_base;

        if (pci_virtio_blk_probe_nth(found, &bar0) != 0) {
            break;
        }

        serial_write("VirtIO block     : legacy probe matched\n");
        if ((bar0 & 1u) != 1u) {
            serial_write("VirtIO block     : unsupported non-legacy BAR\n");
            break;
        }

        io_base = (uint16_t)(bar0 & 0xFFFCu);
        if (virtio_blk_init_legacy(io_base, &g_virtio_blk_devs[found]) != 0) {
            serial_write("VirtIO block     : legacy init failed\n");
            break;
        }

        serial_write("VirtIO block     : legacy initialized\n");
        cap_lo = inl((uint16_t)(io_base + LVIO_CAPACITY_LO));
        cap_hi = inl((uint16_t)(io_base + LVIO_CAPACITY_HI));

        virtio_blk_set_name(&g_block_devices[found], found);
        g_block_devices[found].sector_size = 512u;
        g_block_devices[found].sector_count = ((uint64_t)cap_hi << 32) | (uint64_t)cap_lo;
        g_block_devices[found].present = 1;
        found++;
    }

    if (found == 0u) {
        serial_write("VirtIO block     : PCI device not found\n");
        return -1;
    }

    g_block_device_count = found;
#endif

#ifdef ARCH_AARCH64
    uint64_t common_cfg;
    uint64_t notify_cfg;
    uint64_t device_cfg;
    uint32_t notify_mult;
    uint64_t mmio_base;
    uint32_t cap_lo;
    uint32_t cap_hi;

    common_cfg = 0u;
    notify_cfg = 0u;
    device_cfg = 0u;
    notify_mult = 0u;

    /*
     * Discovery order on AArch64:
     *   1. PCIe (real RPi4+/CM4 direction; QEMU virt can expose PCIe devices)
     *   2. virtio-mmio fallback (QEMU virt built-in transport slots)
     */
    if (aa64_pci_virtio_blk_probe_modern(AA64_QEMU_VIRT_PCIE_ECAM_BASE,
                                         &common_cfg, &notify_cfg,
                                         &device_cfg, &notify_mult) == 0) {
        if (virtio_blk_init_pci_modern(common_cfg, notify_cfg, device_cfg,
                                       notify_mult, &g_virtio_blk_devs[0]) != 0) {
            return -1;
        }
    } else {
        mmio_base = virtio_mmio_blk_probe();
        if (mmio_base == 0UL) {
            return -1;
        }

        if (virtio_blk_init_mmio(mmio_base, &g_virtio_blk_devs[0]) != 0) {
            return -1;
        }
    }

    cap_lo = *(volatile uint32_t *)(uintptr_t)(g_virtio_blk_devs[0].device_cfg_base + 0u);
    cap_hi = *(volatile uint32_t *)(uintptr_t)(g_virtio_blk_devs[0].device_cfg_base + 4u);
    virtio_blk_set_name(&g_block_devices[0], 0u);
    g_block_devices[0].sector_size = 512u;
    g_block_devices[0].sector_count = ((uint64_t)cap_hi << 32) | (uint64_t)cap_lo;
    g_block_devices[0].present = 1;
    g_block_device_count = 1u;
#endif

    g_block_vda = g_block_devices[0];
    g_virtio_blk_dev = g_virtio_blk_devs[0];

    virtio_blk_dump_inventory();

    return 0;
}

/* --------------------------------------------------------------------------
 * InternalCall native implementations
 * --------------------------------------------------------------------------
 *
 * The interpreter dispatches 3-arg (I64, I32, OBJREF) -> I32 calls as:
 *   int32_t fn(int64_t lba, int32_t count, void *arr_obj)
 * -------------------------------------------------------------------------- */

int32_t native_read_sector(int64_t lba, int32_t count, void *arr_obj)
{
    return native_read_sector_device(0, lba, count, arr_obj);
}

int32_t native_read_sector_device(int32_t device_index, int64_t lba, int32_t count, void *arr_obj)
{
    uint8_t *data_ptr;
    int      rc;
    virtio_dev_t *dev;

    if (arr_obj == (void *)0 || count <= 0 || device_index < 0 ||
        (uint32_t)device_index >= g_block_device_count) {
        serial_write("VirtIO block     : read invalid args\n");
        return -1;
    }
    dev = &g_virtio_blk_devs[device_index];
    if (dev->initialized == 0) {
        serial_write("VirtIO block     : read before init\n");
        return -1;
    }

    /* Data starts immediately after the 16-byte managed array header */
    data_ptr = (uint8_t *)arr_obj + CLR_ARR_HDR_SIZE;

    rc = virtio_blk_read(dev, (uint64_t)lba,
                         (uint32_t)count, data_ptr);
    if (rc != 0) {
        serial_write("VirtIO block     : read failed\n");
    }
    return (int32_t)rc;
}

int32_t native_write_sector(int64_t lba, int32_t count, void *arr_obj)
{
    return native_write_sector_device(0, lba, count, arr_obj);
}

int32_t native_write_sector_device(int32_t device_index, int64_t lba, int32_t count, void *arr_obj)
{
    const uint8_t *data_ptr;
    int            rc;
    virtio_dev_t  *dev;

    if (arr_obj == (void *)0 || count <= 0 || device_index < 0 ||
        (uint32_t)device_index >= g_block_device_count) {
        return -1;
    }
    dev = &g_virtio_blk_devs[device_index];
    if (dev->initialized == 0) {
        return -1;
    }

    data_ptr = (const uint8_t *)arr_obj + CLR_ARR_HDR_SIZE;

    rc = virtio_blk_write(dev, (uint64_t)lba,
                          (uint32_t)count, data_ptr);
    return (int32_t)rc;
}

