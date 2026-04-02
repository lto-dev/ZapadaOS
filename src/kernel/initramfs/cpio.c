/*
 * Zapada - src/kernel/initramfs/cpio.c
 *
 * SVR4/newc format cpio archive parser implementation.
 * Handles 070701 (newc) format: ASCII hex header followed by filename and data.
 */

#include "cpio.h"
#include <kernel/support/kernel_memory.h>

/* newc cpio format magic (6 bytes) */
#define CPIO_NEWC_MAGIC_0 0x30  /* '0' */
#define CPIO_NEWC_MAGIC_1 0x37  /* '7' */
#define CPIO_NEWC_MAGIC_2 0x30  /* '0' */
#define CPIO_NEWC_MAGIC_3 0x37  /* '7' */
#define CPIO_NEWC_MAGIC_4 0x30  /* '0' */
#define CPIO_NEWC_MAGIC_5 0x31  /* '1' */

#define CPIO_TRAILER    "TRAILER!!!"

/* Verify magic bytes at position */
static int cpio_check_magic(const uint8_t *ptr)
{
    return ptr[0] == CPIO_NEWC_MAGIC_0 &&
           ptr[1] == CPIO_NEWC_MAGIC_1 &&
           ptr[2] == CPIO_NEWC_MAGIC_2 &&
           ptr[3] == CPIO_NEWC_MAGIC_3 &&
           ptr[4] == CPIO_NEWC_MAGIC_4 &&
           ptr[5] == CPIO_NEWC_MAGIC_5;
}

/* Parse ASCII hex field of given length */
static int cpio_parse_hex(const char *ptr, int len, uint32_t *out)
{
    uint32_t val = 0;
    int i;

    if (!ptr || !out || len < 1)
        return 0;

    for (i = 0; i < len; i++) {
        char c = ptr[i];
        uint32_t digit = 0;

        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            return 0;  /* Invalid character */

        val = (val << 4) | digit;
    }

    *out = val;
    return 1;
}

/* Compare strings byte-by-byte (no libc) */
static int cpio_strcmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 1;
        a++;
        b++;
    }
    return (*a != *b) ? 1 : 0;
}

/* Align position to next multiple of 4 bytes (cpio newc padding) */
static uint32_t cpio_align4(uint32_t pos)
{
    return (pos + 3) & ~3U;
}

/* Initialize iterator for reading archive */
cpio_status_t cpio_iter_init(
    cpio_iterator_t *iter,
    const uint8_t   *archive,
    uint32_t         archive_size)
{
    if (!iter || !archive || archive_size < 6)
        return CPIO_INVALID_FORMAT;

    /* Verify magic at start of archive */
    if (!cpio_check_magic(archive))
        return CPIO_INVALID_FORMAT;

    iter->archive = archive;
    iter->archive_size = archive_size;
    iter->pos = 0;
    iter->entry_count = 0;

    return CPIO_OK;
}

/* Get next entry from archive */
cpio_status_t cpio_iter_next(
    cpio_iterator_t *iter,
    cpio_entry_t    *entry)
{
    uint32_t pos;
    const uint8_t *ptr;
    uint32_t namesize, filesize;

    if (!iter || !entry)
        return CPIO_INVALID_FORMAT;

    if (iter->pos >= iter->archive_size)
        return CPIO_INVALID_FORMAT;

    ptr = iter->archive + iter->pos;

    /* Verify magic at current position */
    if (iter->archive_size - iter->pos < 6)
        return CPIO_INVALID_FORMAT;

    if (!cpio_check_magic(ptr))
        return CPIO_INVALID_FORMAT;

    /* Parse newc header (110 bytes total) */
    if (iter->archive_size - iter->pos < 110)
        return CPIO_INVALID_FORMAT;

    /* Parse fields from header */
    if (!cpio_parse_hex((char*)ptr + 6, 8, &entry->inode))
        return CPIO_INVALID_FORMAT;

    if (!cpio_parse_hex((char*)ptr + 14, 8, &entry->mode))
        return CPIO_INVALID_FORMAT;

    if (!cpio_parse_hex((char*)ptr + 22, 8, &entry->uid))
        return CPIO_INVALID_FORMAT;

    if (!cpio_parse_hex((char*)ptr + 30, 8, &entry->gid))
        return CPIO_INVALID_FORMAT;

    if (!cpio_parse_hex((char*)ptr + 38, 8, &entry->nlink))
        return CPIO_INVALID_FORMAT;

    if (!cpio_parse_hex((char*)ptr + 46, 8, &entry->mtime))
        return CPIO_INVALID_FORMAT;

    if (!cpio_parse_hex((char*)ptr + 54, 8, &filesize))
        return CPIO_INVALID_FORMAT;
    entry->filesize = filesize;

    if (!cpio_parse_hex((char*)ptr + 62, 8, &entry->dev_major))
        return CPIO_INVALID_FORMAT;

    if (!cpio_parse_hex((char*)ptr + 70, 8, &entry->dev_minor))
        return CPIO_INVALID_FORMAT;

    if (!cpio_parse_hex((char*)ptr + 78, 8, &entry->rdev_major))
        return CPIO_INVALID_FORMAT;

    if (!cpio_parse_hex((char*)ptr + 86, 8, &entry->rdev_minor))
        return CPIO_INVALID_FORMAT;

    if (!cpio_parse_hex((char*)ptr + 94, 8, &namesize))
        return CPIO_INVALID_FORMAT;
    entry->namesize = namesize;

    if (!cpio_parse_hex((char*)ptr + 102, 8, &entry->check))
        return CPIO_INVALID_FORMAT;

    /* Header is 110 bytes, then filename (with null terminator) */
    pos = iter->pos + 110;

    if (pos + namesize > iter->archive_size)
        return CPIO_INVALID_FORMAT;

    entry->filename = (const char*)(iter->archive + pos);

    /* Check for TRAILER record */
    if (cpio_strcmp(entry->filename, CPIO_TRAILER) == 0)
        return CPIO_END_OF_ARCHIVE;

    /* Advance past filename to data (aligned to 4 bytes) */
    pos = cpio_align4(pos + namesize);

    if (pos + filesize > iter->archive_size)
        return CPIO_INVALID_FORMAT;

    entry->data = iter->archive + pos;

    /* Advance to next entry */
    iter->pos = cpio_align4(pos + filesize);
    iter->entry_count++;

    return CPIO_OK;
}

/* Extract specific file by name */
cpio_status_t cpio_extract_file(
    const uint8_t *archive,
    uint32_t       archive_size,
    const char    *filename,
    uint8_t       *dst,
    uint32_t       dst_len,
    uint32_t      *out_len)
{
    cpio_iterator_t iter;
    cpio_entry_t entry;
    cpio_status_t status;

    if (!archive || !filename || !dst || !out_len)
        return CPIO_INVALID_FORMAT;

    status = cpio_iter_init(&iter, archive, archive_size);
    if (status != CPIO_OK)
        return status;

    while (1) {
        status = cpio_iter_next(&iter, &entry);

        if (status == CPIO_END_OF_ARCHIVE)
            return CPIO_INVALID_FILENAME;  /* File not found */

        if (status != CPIO_OK)
            return status;

        /* Compare filenames */
        if (cpio_strcmp(entry.filename, filename) == 0) {
            /* Found the file */
            if (entry.filesize > dst_len)
                return CPIO_OUTPUT_OVERFLOW;

            kernel_memcpy(dst, (void*)entry.data, entry.filesize);
            *out_len = entry.filesize;

            return CPIO_OK;
        }
    }
}
