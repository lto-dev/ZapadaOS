/*
 * Zapada - src/kernel/initramfs/cpio.h
 *
 * SVR4/newc format cpio archive parser.
 * Supports reading files from gzip-decompressed cpio archives.
 */

#ifndef KERNEL_INITRAMFS_CPIO_H
#define KERNEL_INITRAMFS_CPIO_H

#include <kernel/types.h>

typedef int cpio_status_t;

#define CPIO_OK                    0
#define CPIO_INVALID_FORMAT       -1
#define CPIO_END_OF_ARCHIVE       -2
#define CPIO_OUTPUT_OVERFLOW      -3
#define CPIO_INVALID_FILENAME     -4

/* Represents a single file entry in the cpio archive */
typedef struct {
    uint32_t  inode;
    uint32_t  mode;
    uint32_t  uid;
    uint32_t  gid;
    uint32_t  nlink;
    uint32_t  mtime;
    uint32_t  filesize;
    uint32_t  dev_major;
    uint32_t  dev_minor;
    uint32_t  rdev_major;
    uint32_t  rdev_minor;
    uint32_t  namesize;      /* Including null terminator */
    uint32_t  check;
    const char *filename;    /* Pointer into archive data */
    const uint8_t *data;     /* Pointer to file data in archive */
} cpio_entry_t;

/* Iterator state for walking the cpio archive */
typedef struct {
    const uint8_t *archive;
    uint32_t       archive_size;
    uint32_t       pos;
    uint32_t       entry_count;
} cpio_iterator_t;

/*
 * Initialize cpio iterator for reading archive
 *
 * Parameters:
 *   iter         - iterator structure to initialize
 *   archive      - pointer to decompressed cpio data
 *   archive_size - size of archive in bytes
 *
 * Returns:
 *   CPIO_OK on success
 *   CPIO_INVALID_FORMAT if archive doesn't start with cpio magic
 */
cpio_status_t cpio_iter_init(
    cpio_iterator_t *iter,
    const uint8_t   *archive,
    uint32_t         archive_size
);

/*
 * Get next entry from cpio archive
 *
 * Parameters:
 *   iter   - initialized iterator
 *   entry  - (output) entry structure to fill
 *
 * Returns:
 *   CPIO_OK if entry was successfully read
 *   CPIO_END_OF_ARCHIVE if reached TRAILER (normal end)
 *   CPIO_INVALID_FORMAT if archive is malformed
 */
cpio_status_t cpio_iter_next(
    cpio_iterator_t *iter,
    cpio_entry_t    *entry
);

/*
 * Extract a specific file by name
 *
 * Parameters:
 *   archive      - pointer to decompressed cpio data
 *   archive_size - size of archive in bytes
 *   filename     - name of file to extract (e.g., "etc/init")
 *   dst          - destination buffer
 *   dst_len      - size of destination buffer
 *   out_len      - (output) bytes written to dst
 *
 * Returns:
 *   CPIO_OK if file was found and extracted
 *   CPIO_INVALID_FORMAT if archive is malformed
 *   CPIO_OUTPUT_OVERFLOW if file doesn't fit in destination
 *   CPIO_INVALID_FILENAME if file not found in archive
 */
cpio_status_t cpio_extract_file(
    const uint8_t *archive,
    uint32_t       archive_size,
    const char    *filename,
    uint8_t       *dst,
    uint32_t       dst_len,
    uint32_t      *out_len
);

#endif /* KERNEL_INITRAMFS_CPIO_H */
