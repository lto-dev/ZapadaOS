/*
 * Zapada - src/kernel/drivers/virtio.c
 *
 * VirtIO split-virtqueue transport layer.
 *
 * Supports two transport back-ends via compile-time selectors:
 *   ARCH_X86_64   — legacy VirtIO PCI (port I/O register access)
 *   ARCH_AARCH64  — modern VirtIO-MMIO v2 (volatile MMIO register access)
 *
 * The descriptor table / available ring / used ring management and the
 * block request submission + synchronous polling are identical for both.
 *
 * Memory layout
 * -------------
 * Legacy (x86_64): two PMM frames in a single contiguous allocation.
 *   Frame 0: desc[64] (1024 B) | avail (132 B) | padding to 4 KB
 *   Frame 1: used ring (516 B)
 *   The device receives the PFN = frame_0_phys >> 12.
 *
 * Modern MMIO (AArch64): three separate PMM frames.
 *   Frame 0: descriptor table (1024 B in a 4 KB page)
 *   Frame 1: available ring  (132 B in a 4 KB page)
 *   Frame 2: used ring       (516 B in a 4 KB page)
 *   Physical addresses for each ring are written to separate MMIO registers.
 *
 * Block request format (§5.2.6, same for both transports):
 *   [16 B header: type, reserved, sector LBA] +
 *   [N * 512 B data buffer]                   +
 *   [1 B status byte filled by device]
 *
 * Phase 3A is synchronous polling; the IRQ handler stub is provided for
 * Phase 3B wiring.
 */

#include <kernel/drivers/virtio.h>
#include <kernel/mm/pmm.h>
#include <kernel/types.h>

/* --------------------------------------------------------------------------
 * Architecture-specific register access
 * --------------------------------------------------------------------------
 * For ARCH_X86_64 (legacy PCI I/O): outl/inl/outw/inw/outb/inb via io.h.
 * For ARCH_AARCH64 (modern MMIO):   volatile 32-bit pointer read/write.
 *
 * On AArch64 with -mstrict-align every MMIO access must be exactly 32 bits
 * wide; no wider unaligned read must be issued by the compiler.
 * -------------------------------------------------------------------------- */

#ifdef ARCH_X86_64
#include <kernel/arch/x86_64/io.h>

/* Legacy VirtIO PCI register offsets (from I/O port base) */
#define LVIO_DEVICE_FEATURES  0x00u   /* 32-bit R  */
#define LVIO_DRIVER_FEATURES  0x04u   /* 32-bit W  */
#define LVIO_QUEUE_PFN        0x08u   /* 32-bit W  — phys_addr >> 12 */
#define LVIO_QUEUE_SIZE       0x0Cu   /* 16-bit R  */
#define LVIO_QUEUE_SELECT     0x0Eu   /* 16-bit W  */
#define LVIO_QUEUE_NOTIFY     0x10u   /* 16-bit W  */
#define LVIO_DEVICE_STATUS    0x12u   /* 8-bit  R/W */
#define LVIO_ISR_STATUS       0x13u   /* 8-bit  R  (clears on read) */
#define LVIO_DEV_CFG_BASE     0x14u   /* device-specific config base */

static void     lreg_w32(uint16_t base, uint32_t off, uint32_t v)  { outl((uint16_t)(base+off), v); }
static uint32_t lreg_r32(uint16_t base, uint32_t off)              { return inl((uint16_t)(base+off)); }
static void     lreg_w16(uint16_t base, uint32_t off, uint16_t v)  { outw((uint16_t)(base+off), v); }
static uint16_t lreg_r16(uint16_t base, uint32_t off)              { return inw((uint16_t)(base+off)); }
static void     lreg_w8 (uint16_t base, uint32_t off, uint8_t  v)  { outb((uint16_t)(base+off), v); }
static uint8_t  lreg_r8 (uint16_t base, uint32_t off)              { return inb((uint16_t)(base+off)); }

static uint32_t align_up_u32(uint32_t value, uint32_t align)
{
    return (value + (align - 1u)) & ~(align - 1u);
}

static void mreg_w16_pci(uint64_t base, uint32_t off, uint16_t v)
{
    volatile uint16_t *r = (volatile uint16_t *)(uintptr_t)(base + off);
    *r = v;
}

static uint16_t mreg_r16_pci(uint64_t base, uint32_t off)
{
    volatile uint16_t *r = (volatile uint16_t *)(uintptr_t)(base + off);
    return *r;
}

static void mreg_w8_pci(uint64_t base, uint32_t off, uint8_t v)
{
    volatile uint8_t *r = (volatile uint8_t *)(uintptr_t)(base + off);
    *r = v;
}

static void mreg_w32_pci(uint64_t base, uint32_t off, uint32_t v)
{
    volatile uint32_t *r = (volatile uint32_t *)(uintptr_t)(base + off);
    *r = v;
}

static uint8_t mreg_r8_pci(uint64_t base, uint32_t off)
{
    volatile uint8_t *r = (volatile uint8_t *)(uintptr_t)(base + off);
    return *r;
}

#endif /* ARCH_X86_64 */

#define PVIO_COMMON_DF_SEL       0x000u
#define PVIO_COMMON_DF           0x004u
#define PVIO_COMMON_GF_SEL       0x008u
#define PVIO_COMMON_GF           0x00Cu
#define PVIO_COMMON_NUM_QUEUES   0x012u
#define PVIO_COMMON_STATUS       0x014u
#define PVIO_COMMON_Q_SEL        0x016u
#define PVIO_COMMON_Q_SIZE       0x018u
#define PVIO_COMMON_Q_ENABLE     0x01Cu
#define PVIO_COMMON_Q_NOTIFY_OFF 0x01Eu
#define PVIO_COMMON_Q_DESC_LO    0x020u
#define PVIO_COMMON_Q_DESC_HI    0x024u
#define PVIO_COMMON_Q_DRIVER_LO  0x028u
#define PVIO_COMMON_Q_DRIVER_HI  0x02Cu
#define PVIO_COMMON_Q_DEVICE_LO  0x030u
#define PVIO_COMMON_Q_DEVICE_HI  0x034u

#ifdef ARCH_AARCH64

/* Modern VirtIO-MMIO register offsets (VirtIO spec §4.2.2) */
#define MVIO_MAGIC               0x000u
#define MVIO_VERSION             0x004u
#define MVIO_DEVICE_ID           0x008u
#define MVIO_VENDOR_ID           0x00Cu
#define MVIO_DEVICE_FEATURES     0x010u
#define MVIO_DEVICE_FEATURES_SEL 0x014u
#define MVIO_DRIVER_FEATURES     0x020u
#define MVIO_DRIVER_FEATURES_SEL 0x024u
#define MVIO_QUEUE_SEL           0x030u
#define MVIO_QUEUE_NUM_MAX       0x034u
#define MVIO_QUEUE_NUM           0x038u
#define MVIO_QUEUE_READY         0x044u
#define MVIO_QUEUE_NOTIFY        0x050u
#define MVIO_INTERRUPT_STATUS    0x060u
#define MVIO_INTERRUPT_ACK       0x064u
#define MVIO_STATUS              0x070u
#define MVIO_QUEUE_DESC_LOW      0x080u
#define MVIO_QUEUE_DESC_HIGH     0x084u
#define MVIO_QUEUE_DRIVER_LOW    0x090u
#define MVIO_QUEUE_DRIVER_HIGH   0x094u
#define MVIO_QUEUE_DEVICE_LOW    0x0A0u
#define MVIO_QUEUE_DEVICE_HIGH   0x0A4u
#define MVIO_CONFIG_GEN          0x0FCu
#define MVIO_CONFIG              0x100u

static void mreg_w32(uint64_t base, uint32_t off, uint32_t v)
{
    volatile uint32_t *r = (volatile uint32_t *)(uintptr_t)(base + off);
    *r = v;
}

static uint32_t mreg_r32(uint64_t base, uint32_t off)
{
    volatile uint32_t *r = (volatile uint32_t *)(uintptr_t)(base + off);
    return *r;
}

/* AArch64 data memory barrier — required before MMIO notify writes */
static void dmb_ish(void)
{
    __asm__ volatile ("dmb ish" ::: "memory");
}

static void mreg_w16(uint64_t base, uint32_t off, uint16_t v)
{
    volatile uint16_t *r = (volatile uint16_t *)(uintptr_t)(base + off);
    *r = v;
}

static uint16_t mreg_r16(uint64_t base, uint32_t off)
{
    volatile uint16_t *r = (volatile uint16_t *)(uintptr_t)(base + off);
    return *r;
}

static void mreg_w8(uint64_t base, uint32_t off, uint8_t v)
{
    volatile uint8_t *r = (volatile uint8_t *)(uintptr_t)(base + off);
    *r = v;
}

static uint8_t mreg_r8(uint64_t base, uint32_t off)
{
    volatile uint8_t *r = (volatile uint8_t *)(uintptr_t)(base + off);
    return *r;
}

#endif /* ARCH_AARCH64 */

/* --------------------------------------------------------------------------
 * Block request header (same for legacy and modern — VirtIO spec §5.2.6)
 * -------------------------------------------------------------------------- */

#define VIRTIO_BLK_T_IN   0u   /* read */
#define VIRTIO_BLK_T_OUT  1u   /* write */

typedef struct {
    uint32_t type;      /* VIRTIO_BLK_T_IN or VIRTIO_BLK_T_OUT */
    uint32_t reserved;  /* must be zero */
    uint64_t sector;    /* LBA */
} virtio_blk_req_hdr_t;  /* 16 bytes */

/* Static per-request scratch buffers (BSS — physically contiguous) */
static virtio_blk_req_hdr_t s_req_hdr;
static uint8_t               s_req_status;

/* --------------------------------------------------------------------------
 * Descriptor table helpers
 * -------------------------------------------------------------------------- */

/*
 * fill_desc - Populate a virtqueue descriptor entry.
 *
 * On AArch64 with -mstrict-align: uint64_t addr is at offset 0 within
 * virtq_desc_t; the struct is placed at 16-byte-aligned positions in the
 * desc table (table is 4KB aligned), so offset 0 is 8-byte aligned.
 * uint32_t len is at offset 8 — naturally 4-byte aligned.
 * uint16_t flags at offset 12, uint16_t next at offset 14 — 2-byte aligned.
 * All accesses are within alignment — no issue with -mstrict-align.
 */
static void fill_desc(virtq_desc_t *d, uint64_t phys, uint32_t len,
                      uint16_t flags, uint16_t next_idx)
{
    d->addr  = phys;
    d->len   = len;
    d->flags = flags;
    d->next  = next_idx;
}

/*
 * submit_chain - Add a descriptor chain to the available ring and notify device.
 *
 * @q          Virtqueue state.
 * @head_idx   Index of the first descriptor in the chain.
 * @base       Device MMIO or I/O port base.
 * @is_legacy  1 = legacy PCI I/O, 0 = modern MMIO.
 */
static void submit_chain(virtio_dev_t *dev, uint16_t head_idx)
{
    virtq_t  *q;
    uint16_t avail_slot;

    q = &dev->queue;

    /* Write the descriptor chain head into the next available ring slot */
    avail_slot = (uint16_t)(q->avail->idx & (uint16_t)(q->queue_size - 1u));
    q->avail->ring[avail_slot] = head_idx;

    /* Compiler barrier: ensure ring slot write is visible before idx bump */
    __asm__ volatile ("" ::: "memory");

    /* Advance the available ring index (wraps naturally at uint16_t overflow) */
    q->avail->idx = (uint16_t)(q->avail->idx + 1u);

    if (dev->is_legacy != 0) {
#ifdef ARCH_X86_64
        lreg_w16((uint16_t)dev->base, LVIO_QUEUE_NOTIFY, 0u);
#endif
        return;
    }

    if (dev->is_modern_pci != 0) {
        uint16_t notify_off;
        uint64_t notify_addr;

        notify_off =
#ifdef ARCH_AARCH64
            mreg_r16(dev->base, PVIO_COMMON_Q_NOTIFY_OFF);
#else
            mreg_r16_pci(dev->base, PVIO_COMMON_Q_NOTIFY_OFF);
#endif
        notify_addr = dev->notify_base
                    + ((uint64_t)notify_off * (uint64_t)dev->notify_off_multiplier);
#ifdef ARCH_AARCH64
        dmb_ish();
        mreg_w16(notify_addr, 0u, 0u);
#else
        mreg_w16_pci(notify_addr, 0u, 0u);
#endif
        return;
    }

#ifdef ARCH_AARCH64
    dmb_ish();
    mreg_w32(dev->base, MVIO_QUEUE_NOTIFY, 0u);
#endif
}

/*
 * poll_used - Spin-wait until the device completes a request.
 *
 * @q    Virtqueue state.
 *
 * Returns the used ring element id (descriptor chain head).
 */
static uint32_t poll_used(virtq_t *q)
{
    uint32_t id;
    uint32_t spins;

    /* Spin until used.idx advances */
    spins = 100000000u;
    while (q->used->idx == q->last_used_idx && spins > 0u) {
        __asm__ volatile ("" ::: "memory");
        spins--;
    }

    if (q->used->idx == q->last_used_idx) {
        return 0xFFFFFFFFu;
    }

    id = q->used->ring[q->last_used_idx & (uint16_t)(q->queue_size - 1u)].id;
    q->last_used_idx = (uint16_t)(q->last_used_idx + 1u);
    return id;
}

/* --------------------------------------------------------------------------
 * Legacy VirtIO PCI initialisation (x86_64 only)
 * -------------------------------------------------------------------------- */

int virtio_blk_init_legacy(uint16_t io_base, virtio_dev_t *dev)
{
#ifdef ARCH_X86_64
    void    *queue_mem;
    uint16_t qsz;
    uint32_t features;
    uint8_t  status;
    uint8_t *base;
    uint32_t avail_bytes;
    uint32_t used_off;
    uint32_t used_bytes;
    uint32_t total_bytes;
    uint32_t page_count;

    /* Reset device */
    lreg_w8(io_base, LVIO_DEVICE_STATUS, VIRTIO_STATUS_RESET);

    /* ACKNOWLEDGE */
    lreg_w8(io_base, LVIO_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);

    /* DRIVER */
    status = lreg_r8(io_base, LVIO_DEVICE_STATUS);
    lreg_w8(io_base, LVIO_DEVICE_STATUS, (uint8_t)(status | VIRTIO_STATUS_DRIVER));

    /*
     * Read device features and negotiate the minimal subset.
     *
     * Writing all host feature bits back is unsafe because some bits require
     * driver support that Phase 3A does not implement. For the synchronous
     * legacy block path we do not require any optional feature bits, so ack
     * none of them explicitly.
     */
    features = lreg_r32(io_base, LVIO_DEVICE_FEATURES);
    (void)features;
    lreg_w32(io_base, LVIO_DRIVER_FEATURES, 0u);

    /* Select queue 0 */
    lreg_w16(io_base, LVIO_QUEUE_SELECT, 0u);
    qsz = lreg_r16(io_base, LVIO_QUEUE_SIZE);
    if (qsz == 0u) {
        return -1;
    }
    if (qsz > VIRTIO_QUEUE_SIZE) {
        return -1;
    }

    avail_bytes = 6u + ((uint32_t)qsz * 2u);
    used_off    = align_up_u32(((uint32_t)qsz * 16u) + avail_bytes, 4096u);
    used_bytes  = 6u + ((uint32_t)qsz * 8u);
    total_bytes = used_off + used_bytes;
    page_count  = total_bytes / 4096u;
    if ((total_bytes & 4095u) != 0u) {
        page_count++;
    }

    /*
     * Allocate enough contiguous PMM frames for the legacy split queue.
     * Layout: [desc | avail | pad-to-4K | used]
     */
    queue_mem = pmm_alloc_contiguous(page_count);
    if (queue_mem == (void *)0) {
        return -1;
    }

    /* Zero the full queue backing region */
    base = (uint8_t *)queue_mem;
    {
        uint32_t i;
        for (i = 0u; i < page_count * 4096u; i++) { base[i] = 0u; }
    }

    dev->queue.desc       = (virtq_desc_t  *)base;
    dev->queue.avail      = (virtq_avail_t *)(base + (uint32_t)qsz * 16u);
    dev->queue.used       = (virtq_used_t  *)(base + used_off);
    dev->queue.queue_size = qsz;
    dev->queue.last_used_idx  = 0u;
    dev->queue.next_free_desc = 0u;

    /* Write queue PFN (physical address >> 12 = frame index) */
    lreg_w32(io_base, LVIO_QUEUE_PFN,
             (uint32_t)((uintptr_t)queue_mem >> 12));

    /* DRIVER_OK — legacy VirtIO does not use FEATURES_OK */
    status = lreg_r8(io_base, LVIO_DEVICE_STATUS);
    lreg_w8(io_base, LVIO_DEVICE_STATUS,
            (uint8_t)(status | VIRTIO_STATUS_DRIVER_OK));

    dev->base        = (uint64_t)io_base;
    dev->is_legacy   = 1;
    dev->initialized = 1;
    return 0;

#else
    /* This path is never called on AArch64. Satisfies the linker. */
    (void)io_base;
    (void)dev;
    return -1;
#endif
}

/* --------------------------------------------------------------------------
 * Modern VirtIO-MMIO initialisation (AArch64 only)
 * -------------------------------------------------------------------------- */

int virtio_blk_init_mmio(uint64_t mmio_base, virtio_dev_t *dev)
{
#ifdef ARCH_AARCH64
    void    *frame_desc;
    void    *frame_avail;
    void    *frame_used;
    uint32_t qsz;
    uint32_t status;
    uint8_t *bd;
    uint8_t *ba;
    uint8_t *bu;
    uint32_t i;

    /* Reset */
    mreg_w32(mmio_base, MVIO_STATUS, VIRTIO_STATUS_RESET);
    /* Spin until device clears Status to 0 after reset */
    {
        uint32_t timeout = 100000u;
        while (mreg_r32(mmio_base, MVIO_STATUS) != 0u && timeout > 0u) {
            timeout--;
        }
    }

    /* ACKNOWLEDGE */
    mreg_w32(mmio_base, MVIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);

    /* DRIVER */
    status = mreg_r32(mmio_base, MVIO_STATUS);
    mreg_w32(mmio_base, MVIO_STATUS, status | VIRTIO_STATUS_DRIVER);

    /* Feature negotiation (page 0): accept all device features */
    mreg_w32(mmio_base, MVIO_DEVICE_FEATURES_SEL, 0u);
    {
        uint32_t feat = mreg_r32(mmio_base, MVIO_DEVICE_FEATURES);
        mreg_w32(mmio_base, MVIO_DRIVER_FEATURES_SEL, 0u);
        mreg_w32(mmio_base, MVIO_DRIVER_FEATURES, feat);
    }
    /* Feature page 1: set VIRTIO_F_VERSION_1 (bit 0 of page 1) */
    mreg_w32(mmio_base, MVIO_DRIVER_FEATURES_SEL, 1u);
    mreg_w32(mmio_base, MVIO_DRIVER_FEATURES, 1u);

    /* FEATURES_OK */
    status = mreg_r32(mmio_base, MVIO_STATUS);
    mreg_w32(mmio_base, MVIO_STATUS, status | VIRTIO_STATUS_FEATURES_OK);

    /* Verify FEATURES_OK was accepted */
    status = mreg_r32(mmio_base, MVIO_STATUS);
    if ((status & VIRTIO_STATUS_FEATURES_OK) == 0u) {
        mreg_w32(mmio_base, MVIO_STATUS, VIRTIO_STATUS_FAILED);
        return -1;
    }

    /* Queue setup for virtqueue 0 */
    mreg_w32(mmio_base, MVIO_QUEUE_SEL, 0u);
    qsz = mreg_r32(mmio_base, MVIO_QUEUE_NUM_MAX);
    if (qsz == 0u) {
        return -1;
    }
    if (qsz > VIRTIO_QUEUE_SIZE) {
        qsz = VIRTIO_QUEUE_SIZE;
    }
    mreg_w32(mmio_base, MVIO_QUEUE_NUM, qsz);

    /* Allocate three PMM frames (one per ring — physically contiguous not required) */
    frame_desc  = pmm_alloc_frame();
    frame_avail = pmm_alloc_frame();
    frame_used  = pmm_alloc_frame();
    if (frame_desc == (void *)0 || frame_avail == (void *)0 || frame_used == (void *)0) {
        return -1;
    }

    /* Zero all three frames */
    bd = (uint8_t *)frame_desc;
    ba = (uint8_t *)frame_avail;
    bu = (uint8_t *)frame_used;
    for (i = 0u; i < 4096u; i++) { bd[i] = 0u; }
    for (i = 0u; i < 4096u; i++) { ba[i] = 0u; }
    for (i = 0u; i < 4096u; i++) { bu[i] = 0u; }

    dev->queue.desc       = (virtq_desc_t  *)frame_desc;
    dev->queue.avail      = (virtq_avail_t *)frame_avail;
    dev->queue.used       = (virtq_used_t  *)frame_used;
    dev->queue.queue_size = (uint16_t)qsz;
    dev->queue.last_used_idx  = 0u;
    dev->queue.next_free_desc = 0u;

    /* Write physical addresses of each ring */
    {
        uint64_t phys_desc  = (uint64_t)(uintptr_t)frame_desc;
        uint64_t phys_avail = (uint64_t)(uintptr_t)frame_avail;
        uint64_t phys_used  = (uint64_t)(uintptr_t)frame_used;

        mreg_w32(mmio_base, MVIO_QUEUE_DESC_LOW,   (uint32_t)(phys_desc  & 0xFFFFFFFFu));
        mreg_w32(mmio_base, MVIO_QUEUE_DESC_HIGH,  (uint32_t)(phys_desc  >> 32));
        mreg_w32(mmio_base, MVIO_QUEUE_DRIVER_LOW, (uint32_t)(phys_avail & 0xFFFFFFFFu));
        mreg_w32(mmio_base, MVIO_QUEUE_DRIVER_HIGH,(uint32_t)(phys_avail >> 32));
        mreg_w32(mmio_base, MVIO_QUEUE_DEVICE_LOW, (uint32_t)(phys_used  & 0xFFFFFFFFu));
        mreg_w32(mmio_base, MVIO_QUEUE_DEVICE_HIGH,(uint32_t)(phys_used  >> 32));
    }

    /* Mark queue as ready */
    mreg_w32(mmio_base, MVIO_QUEUE_READY, 1u);

    /* DRIVER_OK */
    status = mreg_r32(mmio_base, MVIO_STATUS);
    mreg_w32(mmio_base, MVIO_STATUS, status | VIRTIO_STATUS_DRIVER_OK);

    dev->base        = mmio_base;
    dev->notify_base = 0u;
    dev->device_cfg_base = mmio_base + MVIO_CONFIG;
    dev->notify_off_multiplier = 0u;
    dev->is_legacy   = 0;
    dev->is_modern_pci = 0;
    dev->initialized = 1;
    return 0;

#else
    /* Never called on x86_64. Satisfies the linker. */
    (void)mmio_base;
    (void)dev;
    return -1;
#endif
}

int virtio_blk_init_pci_modern(uint64_t common_cfg_base, uint64_t notify_base,
                               uint64_t device_cfg_base,
                               uint32_t notify_off_multiplier,
                               virtio_dev_t *dev)
{
    void    *frame_desc;
    void    *frame_avail;
    void    *frame_used;
    uint16_t qsz;
    uint8_t  status;
    uint8_t *bd;
    uint8_t *ba;
    uint8_t *bu;
    uint32_t i;

    if (dev == (virtio_dev_t *)0) {
        return -1;
    }

#ifdef ARCH_AARCH64
    mreg_w8(common_cfg_base, PVIO_COMMON_STATUS, VIRTIO_STATUS_RESET);
    mreg_w8(common_cfg_base, PVIO_COMMON_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    status = mreg_r8(common_cfg_base, PVIO_COMMON_STATUS);
    mreg_w8(common_cfg_base, PVIO_COMMON_STATUS, (uint8_t)(status | VIRTIO_STATUS_DRIVER));

    mreg_w32(common_cfg_base, PVIO_COMMON_GF_SEL, 0u);
    mreg_w32(common_cfg_base, PVIO_COMMON_GF, 0u);
    mreg_w32(common_cfg_base, PVIO_COMMON_GF_SEL, 1u);
    mreg_w32(common_cfg_base, PVIO_COMMON_GF, 1u);

    status = mreg_r8(common_cfg_base, PVIO_COMMON_STATUS);
    mreg_w8(common_cfg_base, PVIO_COMMON_STATUS, (uint8_t)(status | VIRTIO_STATUS_FEATURES_OK));
    if ((mreg_r8(common_cfg_base, PVIO_COMMON_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0u) {
        mreg_w8(common_cfg_base, PVIO_COMMON_STATUS, VIRTIO_STATUS_FAILED);
        return -1;
    }

    mreg_w16(common_cfg_base, PVIO_COMMON_Q_SEL, 0u);
    qsz = mreg_r16(common_cfg_base, PVIO_COMMON_Q_SIZE);
#else
    mreg_w8_pci(common_cfg_base, PVIO_COMMON_STATUS, VIRTIO_STATUS_RESET);
    mreg_w8_pci(common_cfg_base, PVIO_COMMON_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    status = mreg_r8_pci(common_cfg_base, PVIO_COMMON_STATUS);
    mreg_w8_pci(common_cfg_base, PVIO_COMMON_STATUS, (uint8_t)(status | VIRTIO_STATUS_DRIVER));

    mreg_w32_pci(common_cfg_base, PVIO_COMMON_GF_SEL, 0u);
    mreg_w32_pci(common_cfg_base, PVIO_COMMON_GF, 0u);
    mreg_w32_pci(common_cfg_base, PVIO_COMMON_GF_SEL, 1u);
    mreg_w32_pci(common_cfg_base, PVIO_COMMON_GF, 1u);

    status = mreg_r8_pci(common_cfg_base, PVIO_COMMON_STATUS);
    mreg_w8_pci(common_cfg_base, PVIO_COMMON_STATUS, (uint8_t)(status | VIRTIO_STATUS_FEATURES_OK));
    if ((mreg_r8_pci(common_cfg_base, PVIO_COMMON_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0u) {
        mreg_w8_pci(common_cfg_base, PVIO_COMMON_STATUS, VIRTIO_STATUS_FAILED);
        return -1;
    }

    mreg_w16_pci(common_cfg_base, PVIO_COMMON_Q_SEL, 0u);
    qsz = mreg_r16_pci(common_cfg_base, PVIO_COMMON_Q_SIZE);
#endif

    if (qsz == 0u) {
        return -1;
    }
    if (qsz > VIRTIO_QUEUE_SIZE) {
        qsz = (uint16_t)VIRTIO_QUEUE_SIZE;
    }

    frame_desc  = pmm_alloc_frame();
    frame_avail = pmm_alloc_frame();
    frame_used  = pmm_alloc_frame();
    if (frame_desc == (void *)0 || frame_avail == (void *)0 || frame_used == (void *)0) {
        return -1;
    }

    bd = (uint8_t *)frame_desc;
    ba = (uint8_t *)frame_avail;
    bu = (uint8_t *)frame_used;
    for (i = 0u; i < 4096u; i++) { bd[i] = 0u; }
    for (i = 0u; i < 4096u; i++) { ba[i] = 0u; }
    for (i = 0u; i < 4096u; i++) { bu[i] = 0u; }

    dev->queue.desc       = (virtq_desc_t *)frame_desc;
    dev->queue.avail      = (virtq_avail_t *)frame_avail;
    dev->queue.used       = (virtq_used_t *)frame_used;
    dev->queue.queue_size = qsz;
    dev->queue.last_used_idx  = 0u;
    dev->queue.next_free_desc = 0u;

#ifdef ARCH_AARCH64
    mreg_w16(common_cfg_base, PVIO_COMMON_Q_SEL, 0u);
    mreg_w16(common_cfg_base, PVIO_COMMON_Q_SIZE, qsz);
    mreg_w32(common_cfg_base, PVIO_COMMON_Q_DESC_LO,   (uint32_t)((uint64_t)(uintptr_t)frame_desc  & 0xFFFFFFFFu));
    mreg_w32(common_cfg_base, PVIO_COMMON_Q_DESC_HI,   (uint32_t)((uint64_t)(uintptr_t)frame_desc  >> 32));
    mreg_w32(common_cfg_base, PVIO_COMMON_Q_DRIVER_LO, (uint32_t)((uint64_t)(uintptr_t)frame_avail & 0xFFFFFFFFu));
    mreg_w32(common_cfg_base, PVIO_COMMON_Q_DRIVER_HI, (uint32_t)((uint64_t)(uintptr_t)frame_avail >> 32));
    mreg_w32(common_cfg_base, PVIO_COMMON_Q_DEVICE_LO, (uint32_t)((uint64_t)(uintptr_t)frame_used  & 0xFFFFFFFFu));
    mreg_w32(common_cfg_base, PVIO_COMMON_Q_DEVICE_HI, (uint32_t)((uint64_t)(uintptr_t)frame_used  >> 32));
    mreg_w16(common_cfg_base, PVIO_COMMON_Q_ENABLE, 1u);
    status = mreg_r8(common_cfg_base, PVIO_COMMON_STATUS);
    mreg_w8(common_cfg_base, PVIO_COMMON_STATUS, (uint8_t)(status | VIRTIO_STATUS_DRIVER_OK));
#else
    mreg_w16_pci(common_cfg_base, PVIO_COMMON_Q_SEL, 0u);
    mreg_w16_pci(common_cfg_base, PVIO_COMMON_Q_SIZE, qsz);
    mreg_w32_pci(common_cfg_base, PVIO_COMMON_Q_DESC_LO,   (uint32_t)((uint64_t)(uintptr_t)frame_desc  & 0xFFFFFFFFu));
    mreg_w32_pci(common_cfg_base, PVIO_COMMON_Q_DESC_HI,   (uint32_t)((uint64_t)(uintptr_t)frame_desc  >> 32));
    mreg_w32_pci(common_cfg_base, PVIO_COMMON_Q_DRIVER_LO, (uint32_t)((uint64_t)(uintptr_t)frame_avail & 0xFFFFFFFFu));
    mreg_w32_pci(common_cfg_base, PVIO_COMMON_Q_DRIVER_HI, (uint32_t)((uint64_t)(uintptr_t)frame_avail >> 32));
    mreg_w32_pci(common_cfg_base, PVIO_COMMON_Q_DEVICE_LO, (uint32_t)((uint64_t)(uintptr_t)frame_used  & 0xFFFFFFFFu));
    mreg_w32_pci(common_cfg_base, PVIO_COMMON_Q_DEVICE_HI, (uint32_t)((uint64_t)(uintptr_t)frame_used  >> 32));
    mreg_w16_pci(common_cfg_base, PVIO_COMMON_Q_ENABLE, 1u);
    status = mreg_r8_pci(common_cfg_base, PVIO_COMMON_STATUS);
    mreg_w8_pci(common_cfg_base, PVIO_COMMON_STATUS, (uint8_t)(status | VIRTIO_STATUS_DRIVER_OK));
#endif

    dev->base        = common_cfg_base;
    dev->notify_base = notify_base;
    dev->device_cfg_base = device_cfg_base;
    dev->notify_off_multiplier = notify_off_multiplier;
    dev->is_legacy   = 0;
    dev->is_modern_pci = 1;
    dev->initialized = 1;
    return 0;
}

/* --------------------------------------------------------------------------
 * IRQ handler stub (Phase 3B deferred)
 * -------------------------------------------------------------------------- */

void virtio_blk_irq_handler(void)
{
    /* Intentionally empty in Phase 3A — I/O is synchronous polling only. */
}

/* --------------------------------------------------------------------------
 * Block I/O — synchronous read / write
 *
 * 3-descriptor chain per request:
 *   desc[0] → request header  (16 bytes, device reads)
 *   desc[1] → data buffer     (count*512 bytes, device writes for read)
 *   desc[2] → status byte     (1 byte, device writes)
 *
 * The three descriptors reuse indices 0, 1, 2 because Phase 3A issues one
 * request at a time and waits for completion before returning.
 * -------------------------------------------------------------------------- */

int virtio_blk_read(virtio_dev_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    virtq_t  *q;
    uint64_t  phys_hdr;
    uint64_t  phys_buf;
    uint64_t  phys_stat;
    uint32_t  used_id;

    if (dev == (virtio_dev_t *)0 || dev->initialized == 0) {
        return -1;
    }

    /* Fill request header */
    s_req_hdr.type     = VIRTIO_BLK_T_IN;
    s_req_hdr.reserved = 0u;
    s_req_hdr.sector   = lba;
    s_req_status       = 0xFFu; /* sentinel */

    phys_hdr  = (uint64_t)(uintptr_t)&s_req_hdr;
    phys_buf  = (uint64_t)(uintptr_t)buf;
    phys_stat = (uint64_t)(uintptr_t)&s_req_status;
    q = &dev->queue;

    /* Build 3-descriptor chain at indices 0, 1, 2 */
    fill_desc(&q->desc[0], phys_hdr,  16u,          VIRTQ_DESC_F_NEXT,                   1u);
    fill_desc(&q->desc[1], phys_buf,  count * 512u, VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT, 2u);
    fill_desc(&q->desc[2], phys_stat, 1u,           VIRTQ_DESC_F_WRITE,                   0u);

    /* Submit chain (head = descriptor 0) */
    submit_chain(dev, 0u);

    /* Spin-wait for completion */
    used_id = poll_used(q);
    if (used_id == 0xFFFFFFFFu) {
        return -1;
    }

    /* status == 0 means success */
    if (s_req_status != 0u) {
        return 1;   /* device error */
    }
    return 0;
}

int virtio_blk_write(virtio_dev_t *dev, uint64_t lba, uint32_t count,
                     const void *buf)
{
    virtq_t  *q;
    uint64_t  phys_hdr;
    uint64_t  phys_buf;
    uint64_t  phys_stat;
    uint32_t  used_id;

    if (dev == (virtio_dev_t *)0 || dev->initialized == 0) {
        return -1;
    }

    /* Fill request header */
    s_req_hdr.type     = VIRTIO_BLK_T_OUT;
    s_req_hdr.reserved = 0u;
    s_req_hdr.sector   = lba;
    s_req_status       = 0xFFu;

    phys_hdr  = (uint64_t)(uintptr_t)&s_req_hdr;
    phys_buf  = (uint64_t)(uintptr_t)buf;
    phys_stat = (uint64_t)(uintptr_t)&s_req_status;

    /* Build 3-descriptor chain; data buffer is read by device (no WRITE flag) */
    q = &dev->queue;
    fill_desc(&q->desc[0], phys_hdr,  16u,          VIRTQ_DESC_F_NEXT,  1u);
    fill_desc(&q->desc[1], phys_buf,  count * 512u, VIRTQ_DESC_F_NEXT,  2u);
    fill_desc(&q->desc[2], phys_stat, 1u,           VIRTQ_DESC_F_WRITE, 0u);

    submit_chain(dev, 0u);
    used_id = poll_used(q);
    if (used_id == 0xFFFFFFFFu) {
        return -1;
    }

    if (s_req_status != 0u) {
        return 1;
    }
    return 0;
}

