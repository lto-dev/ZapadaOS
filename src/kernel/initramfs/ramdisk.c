/*
 * Zapada - src/kernel/initramfs/ramdisk.c
 *
 * In-memory ramdisk implementation.
 * Indexes cpio archive contents without duplicating file payloads.
 */

#include "ramdisk.h"
#include "cpio.h"

/* Max files that can be stored in ramdisk */
#define RAMDISK_MAX_FILES 128

/* File entry in ramdisk index */
typedef struct {
    const char *filename;
    uint8_t *data;
    uint32_t size;
    uint32_t mode;
    uint32_t mtime;
} ramdisk_entry_t;

/* Global ramdisk state */
static struct {
    ramdisk_entry_t files[RAMDISK_MAX_FILES];
    uint32_t file_count;
    int initialized;
} g_ramdisk = {
    .file_count = 0,
    .initialized = 0
};

/* Initialize ramdisk from cpio archive storage */
ramdisk_status_t ramdisk_init_from_archive(
    const uint8_t *archive,
    uint32_t       archive_size)
{
    cpio_iterator_t iter;
    cpio_entry_t entry;
    cpio_status_t cpio_status;
    uint32_t entry_idx;

    if (!archive || archive_size == 0)
        return RAMDISK_CORRUPTED;

    if (g_ramdisk.initialized)
        return RAMDISK_OK;  /* Already initialized */

    /* Initialize cpio iterator */
    cpio_status = cpio_iter_init(&iter, archive, archive_size);
    if (cpio_status != CPIO_OK)
        return RAMDISK_CORRUPTED;

    entry_idx = 0;

    /* Iterate through cpio entries and materialize */
    while (1) {
        cpio_status = cpio_iter_next(&iter, &entry);

        if (cpio_status == CPIO_END_OF_ARCHIVE)
            break;  /* Normal end */

        if (cpio_status != CPIO_OK)
            return RAMDISK_CORRUPTED;

        /* Skip if we've exceeded max capacity */
        if (entry_idx >= RAMDISK_MAX_FILES)
            break;

        /* Skip zero-length files and directories */
        if (entry.filesize == 0)
            continue;

        /*
         * The archive backing storage remains valid after bootstrap:
         * - uncompressed initramfs stays in reserved Multiboot module memory
         * - compressed initramfs is decompressed into heap-owned storage
         * Therefore the ramdisk can store pointers directly into the archive
         * instead of duplicating each file payload.
         */
        g_ramdisk.files[entry_idx].filename = entry.filename;
        g_ramdisk.files[entry_idx].data = (uint8_t *)entry.data;
        g_ramdisk.files[entry_idx].size = entry.filesize;
        g_ramdisk.files[entry_idx].mode = entry.mode;
        g_ramdisk.files[entry_idx].mtime = entry.mtime;

        entry_idx++;
    }

    g_ramdisk.file_count = entry_idx;
    g_ramdisk.initialized = 1;

    return RAMDISK_OK;
}

/* Lookup file by name */
ramdisk_status_t ramdisk_lookup(
    const char      *filename,
    ramdisk_file_t  *file)
{
    uint32_t i;

    if (!filename || !file)
        return RAMDISK_NO_SUCH_FILE;

    if (!g_ramdisk.initialized)
        return RAMDISK_CORRUPTED;

    /* Linear search (adequate for bootstrap) */
    for (i = 0; i < g_ramdisk.file_count; i++) {
        ramdisk_entry_t *entry = &g_ramdisk.files[i];
        const char *f = filename;
        const char *e = entry->filename;

        /* Compare filenames byte-by-byte */
        while (*f && *e) {
            if (*f != *e)
                break;
            f++;
            e++;
        }

        /* Match if both strings ended */
        if (*f == 0 && *e == 0) {
            file->filename = entry->filename;
            file->data = entry->data;
            file->size = entry->size;
            file->mode = entry->mode;
            file->mtime = entry->mtime;
            return RAMDISK_OK;
        }
    }

    return RAMDISK_NO_SUCH_FILE;
}

/* Get file count */
uint32_t ramdisk_file_count(void)
{
    return g_ramdisk.file_count;
}

/* Get file by index */
ramdisk_status_t ramdisk_get_file(
    uint32_t         index,
    ramdisk_file_t  *file)
{
    ramdisk_entry_t *entry;

    if (!file)
        return RAMDISK_NO_SUCH_FILE;

    if (!g_ramdisk.initialized)
        return RAMDISK_CORRUPTED;

    if (index >= g_ramdisk.file_count)
        return RAMDISK_NO_SUCH_FILE;

    entry = &g_ramdisk.files[index];
    file->filename = entry->filename;
    file->data     = entry->data;
    file->size     = entry->size;
    file->mode     = entry->mode;
    file->mtime    = entry->mtime;

    return RAMDISK_OK;
}
