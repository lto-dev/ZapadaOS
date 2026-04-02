/*
 * Zapada - src/kernel/arch/aarch64/virtio_mmio.h
 *
 * AArch64 VirtIO-MMIO platform device probe.
 *
 * On the QEMU virt machine the VirtIO MMIO transport is exposed at a fixed
 * base address (0x0A000000) with one slot per 0x200-byte stride.
 *
 * MMIO slots are scanned by checking the Magic register (offset 0x000) and
 * the DeviceID register (offset 0x008).  Device ID 0x02 = block device.
 *
 * Permanent C layer: all volatile MMIO reads stay here.
 */

#ifndef ZAPADA_ARCH_AARCH64_VIRTIO_MMIO_H
#define ZAPADA_ARCH_AARCH64_VIRTIO_MMIO_H

#include <kernel/types.h>

/* QEMU virt machine VirtIO-MMIO base and stride */
#define VIRTIO_MMIO_BASE_ADDR    0x0A000000UL
#define VIRTIO_MMIO_SLOT_STRIDE  0x00000200UL   /* 512 bytes per slot */
#define VIRTIO_MMIO_SLOT_COUNT   32u

/* VirtIO-MMIO register offsets (MMIO transport v2, VirtIO spec §4.2) */
#define VIRTIO_MMIO_REG_MAGIC          0x000u   /* R:  must read 0x74726976 */
#define VIRTIO_MMIO_REG_VERSION        0x004u   /* R:  1 = legacy, 2 = modern */
#define VIRTIO_MMIO_REG_DEVICE_ID      0x008u   /* R:  device type */
#define VIRTIO_MMIO_REG_VENDOR_ID      0x00Cu   /* R:  vendor */

/* Expected magic value ("virt" in LE ASCII) */
#define VIRTIO_MMIO_MAGIC_VALUE  0x74726976UL

/* VirtIO device type IDs */
#define VIRTIO_DEVICE_ID_BLOCK   0x02u          /* block device */

/*
 * virtio_mmio_blk_probe - Scan the platform MMIO bus for a VirtIO block device.
 *
 * Returns the MMIO base address of the first block device slot found,
 * or 0 if no block device is present.
 */
uint64_t virtio_mmio_blk_probe(void);

#endif /* ZAPADA_ARCH_AARCH64_VIRTIO_MMIO_H */


