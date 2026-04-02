/*
 * Zapada - src/kernel/initramfs/ramdisk.h
 *
 * In-memory ramdisk for initramfs contents indexed in place.
 * Provides simple file lookup and access to files stored in the backing cpio archive.
 */

#ifndef KERNEL_INITRAMFS_RAMDISK_H
#define KERNEL_INITRAMFS_RAMDISK_H

#include <kernel/types.h>

typedef int ramdisk_status_t;

#define RAMDISK_OK              0
#define RAMDISK_NO_MEMORY       -1
#define RAMDISK_NO_SUCH_FILE    -2
#define RAMDISK_CORRUPTED       -3

/* Metadata for a file materialized in ramdisk */
typedef struct {
    const char *filename;
    uint8_t *data;
    uint32_t size;
    uint32_t mode;
    uint32_t mtime;
} ramdisk_file_t;

/*
 * Initialize ramdisk from decompressed cpio archive.
 *
 * This function:
 * 1. Parses the cpio archive from the decompressed buffer
 * 2. Builds an internal index for fast lookup
 * 3. Stores file-name and file-data pointers into the backing archive storage
 *
 * The caller must ensure the archive storage remains valid for the lifetime of
 * the ramdisk. This is true for both:
 * - reserved Multiboot initramfs module memory
 * - heap-owned decompressed archive buffers
 *
 * Parameters:
 *   archive      - decompressed cpio archive data
 *   archive_size - size of archive in bytes
 *
 * Returns:
 *   RAMDISK_OK on success
 *   RAMDISK_NO_MEMORY if unable to allocate memory
 *   RAMDISK_CORRUPTED if archive is malformed
 */
ramdisk_status_t ramdisk_init_from_archive(
    const uint8_t *archive,
    uint32_t       archive_size
);

/*
 * Lookup file by name in the ramdisk.
 *
 * Parameters:
 *   filename - file path relative to root (e.g., "drivers/nvme.dll")
 *   file     - (output) populated with file metadata and data pointer
 *
 * Returns:
 *   RAMDISK_OK if file found
 *   RAMDISK_NO_SUCH_FILE if not found
 */
ramdisk_status_t ramdisk_lookup(
    const char      *filename,
    ramdisk_file_t  *file
);

/*
 * Get total number of files in ramdisk.
 *
 * Returns the count of files that were materialized.
 */
uint32_t ramdisk_file_count(void);

/*
 * Get file by index in the ramdisk.
 *
 * Parameters:
 *   index - 0-based file index (must be < ramdisk_file_count())
 *   file  - (output) populated with file metadata and data pointer
 *
 * Returns:
 *   RAMDISK_OK if index is valid
 *   RAMDISK_NO_SUCH_FILE if index is out of range
 */
ramdisk_status_t ramdisk_get_file(
    uint32_t         index,
    ramdisk_file_t  *file
);

#endif /* KERNEL_INITRAMFS_RAMDISK_H */
