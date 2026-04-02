/*
 * Zapada - src/kernel/initramfs/tinflate.h
 *
 * Minimal deflate decompressor for kernel-space gzip/zlib decompression.
 * Based on tinflate (public domain).
 *
 * Supports:
 *   - RFC 1951 (DEFLATE)
 *   - RFC 1950 (zlib wrapper)
 *   - RFC 1952 (gzip wrapper)
 *
 * Input: compressed data with optional gzip/zlib headers
 * Output: decompressed data
 */

#ifndef KERNEL_INITRAMFS_TINFLATE_H
#define KERNEL_INITRAMFS_TINFLATE_H

#include <kernel/types.h>

typedef int tinflate_status_t;

/* Status codes */
#define TINFLATE_OK                0
#define TINFLATE_INVALID_DATA     -1
#define TINFLATE_OUTPUT_OVERFLOW  -2
#define TINFLATE_INCOMPLETE       -3

/*
 * tinflate_decompress: Decompress a block of data
 *
 * Parameters:
 *   src         - source (compressed) data
 *   src_len     - size of source data in bytes
 *   dst         - destination (output) buffer
 *   dst_len     - size of destination buffer in bytes
 *   out_len     - (output) number of bytes written to dst
 *
 * Returns:
 *   TINFLATE_OK on success
 *   TINFLATE_INVALID_DATA if input is malformed
 *   TINFLATE_OUTPUT_OVERFLOW if output buffer is too small
 *   TINFLATE_INCOMPLETE if input is truncated
 */
tinflate_status_t tinflate_decompress(
    const uint8_t *src,
    uint32_t       src_len,
    uint8_t       *dst,
    uint32_t       dst_len,
    uint32_t      *out_len
);

#endif /* KERNEL_INITRAMFS_TINFLATE_H */
