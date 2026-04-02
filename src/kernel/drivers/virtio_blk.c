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

virtio_dev_t  g_virtio_blk_dev;   /* Zero-initialised in BSS until probe */
block_device_t g_block_vda;       /* Zero-initialised in BSS until probe */

/* --------------------------------------------------------------------------
 * virtio_blk_probe_and_init
 * -------------------------------------------------------------------------- */

int virtio_blk_probe_and_init(void)
{
#ifdef ARCH_X86_64
    uint32_t bar0;
    uint32_t cap_lo;
    uint32_t cap_hi;
    uint64_t common_cfg;
    uint64_t notify_cfg;
    uint64_t device_cfg;
    uint32_t notify_mult;
    int ret;

    common_cfg = 0u;
    notify_cfg = 0u;
    device_cfg = 0u;
    notify_mult = 0u;

    if (pci_virtio_blk_probe_modern(&common_cfg, &notify_cfg, &device_cfg,
                                    &notify_mult) == 0) {
        if (virtio_blk_init_pci_modern(common_cfg, notify_cfg, device_cfg,
                                       notify_mult, &g_virtio_blk_dev) != 0) {
            return -1;
        }

        cap_lo = *(volatile uint32_t *)(uintptr_t)(g_virtio_blk_dev.device_cfg_base + 0u);
        cap_hi = *(volatile uint32_t *)(uintptr_t)(g_virtio_blk_dev.device_cfg_base + 4u);
        g_block_vda.sector_count = ((uint64_t)cap_hi << 32) | (uint64_t)cap_lo;
    } else {

    /*
     * Scan PCI bus 0 for a VirtIO block device (legacy 0x1001 or modern 0x1042).
     * bar0 is the raw BAR0 config-space value.
     */
    ret = pci_virtio_blk_probe(&bar0);
    if (ret != 0) {
        return -1;   /* no VirtIO block device on bus 0 */
    }

    if ((bar0 & 1u) == 1u) {
        /* I/O space BAR — legacy VirtIO PCI */
        uint16_t io_base = (uint16_t)(bar0 & 0xFFFCu);

        if (virtio_blk_init_legacy(io_base, &g_virtio_blk_dev) != 0) {
                return -1;
            }
    
            /*
             * Read device capacity from VirtIO block device config space.
             * In legacy VirtIO PCI, the device-specific config starts at I/O port
             * base + LVIO_DEV_CFG_BASE (0x14).  First 8 bytes = 64-bit sector count.
             */
            cap_lo = inl((uint16_t)(io_base + LVIO_CAPACITY_LO));
            cap_hi = inl((uint16_t)(io_base + LVIO_CAPACITY_HI));
            g_block_vda.sector_count = ((uint64_t)cap_hi << 32) | (uint64_t)cap_lo;
    } else {
        /* MMIO BAR — modern VirtIO PCI (non-transitional) */
        /* Not supported in Phase 3A; return error */
        return -1;
    }
    }
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
                                       notify_mult, &g_virtio_blk_dev) != 0) {
            return -1;
        }
    } else {
        mmio_base = virtio_mmio_blk_probe();
        if (mmio_base == 0UL) {
            return -1;
        }

        if (virtio_blk_init_mmio(mmio_base, &g_virtio_blk_dev) != 0) {
            return -1;
        }
    }

    cap_lo = *(volatile uint32_t *)(uintptr_t)(g_virtio_blk_dev.device_cfg_base + 0u);
    cap_hi = *(volatile uint32_t *)(uintptr_t)(g_virtio_blk_dev.device_cfg_base + 4u);
    g_block_vda.sector_count = ((uint64_t)cap_hi << 32) | (uint64_t)cap_lo;
#endif

    /* Populate the block device descriptor */
    g_block_vda.name[0] = 'v';
    g_block_vda.name[1] = 'd';
    g_block_vda.name[2] = 'a';
    g_block_vda.name[3] = '\0';
    g_block_vda.sector_size = 512u;
    g_block_vda.present     = 1;
    g_virtio_blk_dev.initialized = 1;

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
    uint8_t *data_ptr;
    int      rc;

    if (arr_obj == (void *)0 || count <= 0) {
        return -1;
    }
    if (g_virtio_blk_dev.initialized == 0) {
        return -1;
    }

    /* Data starts immediately after the 16-byte managed array header */
    data_ptr = (uint8_t *)arr_obj + CLR_ARR_HDR_SIZE;

    rc = virtio_blk_read(&g_virtio_blk_dev, (uint64_t)lba,
                         (uint32_t)count, data_ptr);
    return (int32_t)rc;
}

int32_t native_write_sector(int64_t lba, int32_t count, void *arr_obj)
{
    const uint8_t *data_ptr;
    int            rc;

    if (arr_obj == (void *)0 || count <= 0) {
        return -1;
    }
    if (g_virtio_blk_dev.initialized == 0) {
        return -1;
    }

    data_ptr = (const uint8_t *)arr_obj + CLR_ARR_HDR_SIZE;

    rc = virtio_blk_write(&g_virtio_blk_dev, (uint64_t)lba,
                          (uint32_t)count, data_ptr);
    return (int32_t)rc;
}

