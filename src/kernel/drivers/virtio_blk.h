/*
 * Zapada - src/kernel/drivers/virtio_blk.h
 *
 * VirtIO block device InternalCall interface.
 *
 * Exposes ReadSector / WriteSector as InternalCalls to managed C# code.
 * These are the only entry points from the managed layer into the VirtIO
 * transport.  Everything below stays in C permanently.
 *
 * Also exposes the top-level initialisation function used by phase3a_part2.c.
 */

#ifndef ZAPADA_DRIVERS_VIRTIO_BLK_H
#define ZAPADA_DRIVERS_VIRTIO_BLK_H

#include <kernel/types.h>
#include <kernel/drivers/block.h>
#include <kernel/drivers/virtio.h>

/*
 * g_virtio_blk_dev - Global VirtIO block device context.
 * Defined in virtio_blk.c; zeroed until virtio_blk_probe_and_init().
 */
extern virtio_dev_t g_virtio_blk_dev;

/*
 * virtio_blk_probe_and_init - Discover and initialise the VirtIO block device.
 *
 * Called once from phase3a_part2_init().  On success, g_virtio_blk_dev.initialized
 * is set to 1 and g_block_vda is populated.  Returns 0 on success, -1 if no
 * device is found or initialisation fails.
 */
int virtio_blk_probe_and_init(void);

/*
 * virtio_blk_dump_inventory - Print the native block-device inventory.
 */
void virtio_blk_dump_inventory(void);

/*
 * native_read_sector - C handler for the ReadSector InternalCall.
 *
 * Managed signature:
 *   [MethodImpl(MethodImplOptions.InternalCall)]
 *   internal static extern int ReadSector(long lba, int count, int[] buf);
 *
 * The interpreter dispatches 3-arg (I64, I32, OBJREF) -> I32 calls as:
 *   int32_t fn(int64_t lba, int32_t count, void *arr_obj)
 *
 * @lba    Starting 512-byte sector LBA (signed; managed uses long).
 * @count  Number of sectors to read (signed; managed uses int).
 * @arr_obj Managed int[] array object pointer; element data at offset +16.
 *
 * Returns 0 on success, non-zero on error.
 */
int32_t native_read_sector(int64_t lba, int32_t count, void *arr_obj);

/*
 * native_write_sector - C handler for the WriteSector InternalCall.
 *
 * Managed signature:
 *   [MethodImpl(MethodImplOptions.InternalCall)]
 *   internal static extern int WriteSector(long lba, int count, int[] buf);
 */
int32_t native_write_sector(int64_t lba, int32_t count, void *arr_obj);

#endif /* ZAPADA_DRIVERS_VIRTIO_BLK_H */


