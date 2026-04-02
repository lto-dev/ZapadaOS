/*
 * Zapada - src/kernel/drivers/virtio.h
 *
 * VirtIO split-virtqueue transport layer.
 *
 * Supports three transport back-ends:
 *   - legacy VirtIO PCI over port I/O
 *   - modern VirtIO PCI 1.0 via PCI capabilities
 *   - modern VirtIO-MMIO v2
 *
 * The upper half (descriptor table, available/used ring management, request
 * building, synchronous polling) is identical for both back-ends.
 *
 * Queue size is fixed at VIRTIO_QUEUE_SIZE=64 for Phase 3A.
 * Interrupt-driven I/O (DRIVER_NOTIFY bit, IRQ handler) deferred to Phase 3B.
 *
 * Permanent C layer: all raw MMIO / port-I/O stays here and in arch/x86_64/pci.c
 * or arch/aarch64/virtio_mmio.c.  Nothing above this layer touches hardware
 * registers directly.
 */

#ifndef ZAPADA_DRIVERS_VIRTIO_H
#define ZAPADA_DRIVERS_VIRTIO_H

#include <kernel/types.h>

/* --------------------------------------------------------------------------
 * Queue parameters
 * -------------------------------------------------------------------------- */

#define VIRTIO_QUEUE_SIZE  256u         /* descriptors per queue (must be power of 2) */

/* --------------------------------------------------------------------------
 * VirtIO status register bits (spec §2.1)
 * -------------------------------------------------------------------------- */

#define VIRTIO_STATUS_RESET       0x00u
#define VIRTIO_STATUS_ACKNOWLEDGE 0x01u
#define VIRTIO_STATUS_DRIVER      0x02u
#define VIRTIO_STATUS_DRIVER_OK   0x04u
#define VIRTIO_STATUS_FEATURES_OK 0x08u
#define VIRTIO_STATUS_FAILED      0x80u

/* --------------------------------------------------------------------------
 * Virtqueue descriptor flags (spec §2.6.5)
 * -------------------------------------------------------------------------- */

#define VIRTQ_DESC_F_NEXT   0x0001u     /* buffer is continued via .next */
#define VIRTQ_DESC_F_WRITE  0x0002u     /* device writes to this buffer  */

/* --------------------------------------------------------------------------
 * On-device data structures (spec §2.6)
 *
 * All structs must be naturally aligned within their allocation.
 * No __attribute__((packed)) is used — fields are already aligned for
 * the sizes involved.
 * -------------------------------------------------------------------------- */

/* Descriptor table entry — 16 bytes */
typedef struct {
    uint64_t addr;      /* physical address of buffer */
    uint32_t len;       /* length of buffer in bytes  */
    uint16_t flags;     /* VIRTQ_DESC_F_* bits         */
    uint16_t next;      /* index of next descriptor   */
} virtq_desc_t;

/* Available ring (driver → device) */
typedef struct {
    uint16_t flags;                         /* always 0 in Phase 3A */
    uint16_t idx;                           /* next slot to write   */
    uint16_t ring[VIRTIO_QUEUE_SIZE];       /* descriptor chain heads */
} virtq_avail_t;

/* Used ring element — 8 bytes */
typedef struct {
    uint32_t id;    /* descriptor chain head index */
    uint32_t len;   /* bytes written by device     */
} virtq_used_elem_t;

/* Used ring (device → driver) */
typedef struct {
    uint16_t          flags;                /* always 0 in Phase 3A */
    uint16_t          idx;                  /* next slot device will write */
    virtq_used_elem_t ring[VIRTIO_QUEUE_SIZE];
} virtq_used_t;

/* --------------------------------------------------------------------------
 * Internal queue state
 * -------------------------------------------------------------------------- */

typedef struct {
    virtq_desc_t  *desc;            /* virtual pointer to descriptor table   */
    virtq_avail_t *avail;           /* virtual pointer to available ring      */
    virtq_used_t  *used;            /* virtual pointer to used ring           */
    uint16_t       queue_size;      /* number of descriptors                  */
    uint16_t       last_used_idx;   /* tracks used.idx for polling            */
    uint16_t       next_free_desc;  /* circular free-list head (Phase 3A: always 0) */
} virtq_t;

/* --------------------------------------------------------------------------
 * Device context
 * -------------------------------------------------------------------------- */

typedef struct {
    uint64_t  base;         /* common cfg/MMIO base or I/O port base */
    uint64_t  notify_base;  /* modern PCI notify capability base      */
    uint64_t  device_cfg_base; /* modern PCI or MMIO device config base */
    uint32_t  notify_off_multiplier; /* modern PCI notify multiplier    */
    int       is_legacy;    /* 1 = legacy PCI I/O, 0 = non-legacy     */
    int       is_modern_pci;/* 1 = PCI modern transport, 0 otherwise   */
    virtq_t   queue;        /* virtqueue 0 (block requests)                  */
    int       initialized;  /* 1 if transport is ready                       */
} virtio_dev_t;

/* --------------------------------------------------------------------------
 * Transport initialisation
 *
 * Exactly one of the two functions is called per run depending on
 * which arch back-end discovered the device.
 * -------------------------------------------------------------------------- */

/*
 * virtio_blk_init_legacy - Initialise legacy VirtIO PCI block device.
 *
 * @io_base  I/O port base (BAR0 with bit 0 stripped).
 * @dev      Device context to populate.
 *
 * Returns 0 on success, -1 on error.
 */
int virtio_blk_init_legacy(uint16_t io_base, virtio_dev_t *dev);

/*
 * virtio_blk_init_mmio - Initialise modern VirtIO-MMIO block device.
 *
 * @mmio_base  MMIO base address from the MMIO probe.
 * @dev        Device context to populate.
 *
 * Returns 0 on success, -1 on error.
 */
int virtio_blk_init_mmio(uint64_t mmio_base, virtio_dev_t *dev);

int virtio_blk_init_pci_modern(uint64_t common_cfg_base, uint64_t notify_base,
                               uint64_t device_cfg_base,
                               uint32_t notify_off_multiplier,
                               virtio_dev_t *dev);

/*
 * virtio_blk_irq_handler - Placeholder IRQ handler (deferred to Phase 3B).
 *
 * Called by the IRQ dispatcher stub.  Does nothing in Phase 3A.
 */
void virtio_blk_irq_handler(void);

/* --------------------------------------------------------------------------
 * Block I/O
 * -------------------------------------------------------------------------- */

/*
 * virtio_blk_read - Issue a synchronous sector read.
 *
 * @dev    Initialised device context.
 * @lba    Starting logical block address (512-byte sectors).
 * @count  Number of sectors to read.
 * @buf    Destination buffer (must be count * 512 bytes).
 *
 * Returns 0 on success, -1 on transport error, 1 on device error.
 */
int virtio_blk_read(virtio_dev_t *dev, uint64_t lba, uint32_t count, void *buf);

/*
 * virtio_blk_write - Issue a synchronous sector write.
 *
 * @dev    Initialised device context.
 * @lba    Starting logical block address.
 * @count  Number of sectors to write.
 * @buf    Source buffer (must be count * 512 bytes).
 *
 * Returns 0 on success, -1 on transport error, 1 on device error.
 */
int virtio_blk_write(virtio_dev_t *dev, uint64_t lba, uint32_t count,
                     const void *buf);

#endif /* ZAPADA_DRIVERS_VIRTIO_H */


