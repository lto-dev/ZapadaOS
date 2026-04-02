/*
 * Zapada - src/kernel/drivers/block.h
 *
 * Block device descriptor.
 *
 * A block_device_t is a static descriptor for a discovered storage device.
 * Phase 3A supports exactly one device ("vda") backed by a VirtIO block
 * device.  All fields are populated during phase3a_part2_init().
 *
 * Phase 3.1 will replace the static array with a proper device registry.
 */

#ifndef ZAPADA_DRIVERS_BLOCK_H
#define ZAPADA_DRIVERS_BLOCK_H

#include <kernel/types.h>

/* Maximum length of device name including NUL */
#define BLOCK_DEV_NAME_MAX  8u

/*
 * block_device_t - Descriptor for a single block device.
 *
 * @name          Human-readable device name (e.g. "vda")
 * @sector_size   Bytes per logical sector (always 512 for VirtIO block)
 * @sector_count  Total number of 512-byte sectors on the device
 * @present       Set to 1 once the device is discovered and ready
 */
typedef struct {
    char     name[BLOCK_DEV_NAME_MAX];
    uint32_t sector_size;
    uint64_t sector_count;
    int      present;
} block_device_t;

/*
 * g_block_vda - The single Phase-3A VirtIO block device descriptor.
 * Defined in virtio_blk.c; zeroed until phase3a_part2_init() runs.
 */
extern block_device_t g_block_vda;

#endif /* ZAPADA_DRIVERS_BLOCK_H */


