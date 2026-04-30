/*
 * Zapada - src/kernel/drivers/block.h
 *
 * Block device descriptor.
 *
 * A block_device_t is a static descriptor for a discovered storage device.
 * Native keeps a bounded early inventory for the compatibility bridge; managed
 * code owns the higher-level BlockDeviceRegistry.
 */

#ifndef ZAPADA_DRIVERS_BLOCK_H
#define ZAPADA_DRIVERS_BLOCK_H

#include <kernel/types.h>

/* Maximum length of device name including NUL */
#define BLOCK_DEV_NAME_MAX  8u
#define BLOCK_DEV_MAX       4u

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

/* Compatibility descriptor for the first VirtIO block device. */
extern block_device_t g_block_vda;
extern block_device_t g_block_devices[BLOCK_DEV_MAX];
extern uint32_t g_block_device_count;

#endif /* ZAPADA_DRIVERS_BLOCK_H */


