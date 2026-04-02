/*
 * Zapada - src/kernel/initramfs/bootstrap.c
 *
 * Early initramfs bootstrap and verification.
 */

#include <kernel/initramfs/bootstrap.h>
#include <kernel/console.h>
#include <kernel/initramfs/ramdisk.h>
#include <kernel/support/kernel_memory.h>
#include <kernel/initramfs/tinflate.h>

static int initramfs_is_cpio_newc(const uint8_t *data, uint32_t size);

static uint32_t initramfs_expected_size(const uint8_t *module_start, uint32_t module_size)
{
    uint32_t isize;

    if (module_start == 0 || module_size < 18u) {
        return 0u;
    }

    if (module_start[0] == 0x1fu && module_start[1] == 0x8bu) {
        isize = (uint32_t)module_start[module_size - 4u]
              | ((uint32_t)module_start[module_size - 3u] << 8u)
              | ((uint32_t)module_start[module_size - 2u] << 16u)
              | ((uint32_t)module_start[module_size - 1u] << 24u);
        if (isize != 0u) {
            return isize;
        }
    }

    isize = module_size * 2048u;
    if (isize < (64u * 1024u)) {
        isize = 64u * 1024u;
    }
    if (isize > (4u * 1024u * 1024u)) {
        isize = 4u * 1024u * 1024u;
    }
    return isize;
}

uint32_t initramfs_required_heap_bytes(const uint8_t *module_start, uint32_t module_size)
{
    uint32_t expected_size;
    uint32_t required_size;

    if (module_start == 0 || module_size == 0u) {
        return 0u;
    }

    if (initramfs_is_cpio_newc(module_start, module_size)) {
        return 0u;
    }

    expected_size = initramfs_expected_size(module_start, module_size);
    if (expected_size == 0u) {
        return 0u;
    }

    required_size = expected_size + (2u * 1024u * 1024u);
    if (required_size < expected_size) {
        return expected_size;
    }

    return required_size;
}

static int initramfs_is_cpio_newc(const uint8_t *data, uint32_t size)
{
    if (data == 0 || size < 6u) {
        return 0;
    }

    return data[0] == (uint8_t)'0'
        && data[1] == (uint8_t)'7'
        && data[2] == (uint8_t)'0'
        && data[3] == (uint8_t)'7'
        && data[4] == (uint8_t)'0'
        && data[5] == (uint8_t)'1';
}

static void initramfs_log_lookup(const char *name)
{
    ramdisk_file_t file;
    ramdisk_status_t status;

    console_write("  Lookup           : ");
    console_write(name);

    status = ramdisk_lookup(name, &file);
    if (status != RAMDISK_OK) {
        console_write(" missing\n");
        return;
    }

    console_write(" size=");
    console_write_dec((uint64_t)file.size);
    console_write(" bytes\n");
}

void initramfs_bootstrap(const uint8_t *module_start, uint32_t module_size)
{
    uint32_t expected_size;
    uint8_t *archive;
    uint8_t *archive_copy;
    uint32_t archive_size;
    tinflate_status_t tinflate_status;
    ramdisk_status_t ramdisk_status;

    if (module_start == 0 || module_size == 0u) {
        console_write("Initramfs bootstrap: skipped (no module payload)\n");
        return;
    }

    if (initramfs_is_cpio_newc(module_start, module_size)) {
        console_write("Initramfs bootstrap: archive already uncompressed size=");
        console_write_dec((uint64_t)module_size);
        console_write(" bytes\n");

        /*
         * The Multiboot initramfs module already lives in reserved boot memory.
         * Avoid cloning large uncompressed archives into the early heap; the
         * ramdisk indexes archive entries in place.
         */
        archive_copy = (uint8_t *)module_start;

        ramdisk_status = ramdisk_init_from_archive(archive_copy, module_size);
        console_write("Initramfs bootstrap: ramdisk ");
        if (ramdisk_status != RAMDISK_OK) {
            console_write("FAILED status=");
            console_write_dec((uint64_t)(uint32_t)(-ramdisk_status));
            console_write("\n");
            return;
        }

        console_write("OK files=");
        console_write_dec((uint64_t)ramdisk_file_count());
        console_write("\n");

        initramfs_log_lookup("System.Private.CoreLib.dll");
        initramfs_log_lookup("System.dll");
        initramfs_log_lookup("System.Runtime.dll");
        initramfs_log_lookup("Zapada.Boot.dll");
        initramfs_log_lookup("Zapada.Drivers.VirtioBlock.dll");
        initramfs_log_lookup("Zapada.Fs.Gpt.dll");
        initramfs_log_lookup("Zapada.Storage.dll");
        initramfs_log_lookup("Zapada.Fs.Fat32.dll");
        initramfs_log_lookup("Zapada.Fs.Vfs.dll");
        initramfs_log_lookup("Zapada.Conformance.dll");
        initramfs_log_lookup("Zapada.Test.Hello.dll");
        return;
    }

    expected_size = initramfs_expected_size(module_start, module_size);
    if (expected_size == 0u) {
        console_write("Initramfs bootstrap: invalid expected size\n");
        return;
    }

    console_write("Initramfs bootstrap: expected size=");
    console_write_dec((uint64_t)expected_size);
    console_write(" bytes\n");

    archive = (uint8_t *)kernel_alloc(expected_size);
    if (archive == 0) {
        console_write("Initramfs bootstrap: output buffer allocation failed\n");
        return;
    }

    archive_size = 0u;
    tinflate_status = tinflate_decompress(module_start, module_size, archive, expected_size, &archive_size);

    console_write("Initramfs bootstrap: decompress ");
    if (tinflate_status != TINFLATE_OK) {
        console_write("FAILED status=");
        console_write_dec((uint64_t)(uint32_t)(-tinflate_status));
        console_write("\n");
        return;
    }

    console_write("OK size=");
    console_write_dec((uint64_t)archive_size);
    console_write(" bytes\n");

    ramdisk_status = ramdisk_init_from_archive(archive, archive_size);
    console_write("Initramfs bootstrap: ramdisk ");
    if (ramdisk_status != RAMDISK_OK) {
        console_write("FAILED status=");
        console_write_dec((uint64_t)(uint32_t)(-ramdisk_status));
        console_write("\n");
        return;
    }

    console_write("OK files=");
    console_write_dec((uint64_t)ramdisk_file_count());
    console_write("\n");

    initramfs_log_lookup("System.Private.CoreLib.dll");
    initramfs_log_lookup("System.dll");
    initramfs_log_lookup("System.Runtime.dll");
    initramfs_log_lookup("Zapada.Boot.dll");
    initramfs_log_lookup("Zapada.Drivers.VirtioBlock.dll");
    initramfs_log_lookup("Zapada.Fs.Gpt.dll");
    initramfs_log_lookup("Zapada.Storage.dll");
    initramfs_log_lookup("Zapada.Fs.Fat32.dll");
    initramfs_log_lookup("Zapada.Fs.Vfs.dll");
    initramfs_log_lookup("Zapada.Conformance.dll");
    initramfs_log_lookup("Zapada.Test.Hello.dll");
}
