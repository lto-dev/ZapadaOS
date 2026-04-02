/*
 * Zapada - src/kernel/initramfs/loader.c
 *
 * Phase 3: pre-load managed DLL assemblies from the initramfs ramdisk
 * into the managed runtime assembly registry.
 *
 * This runs after initramfs_bootstrap() materializes the ramdisk and before
 * managed_runtime_run() executes the managed boot entry point. The initramfs
 * must therefore contain Zapada.Boot.dll as well as the managed assemblies
 * that BootLoader discovers by name.
 */

#include <kernel/initramfs/loader.h>
#include <kernel/initramfs/ramdisk.h>
#include <kernel/console.h>

#if !defined(ZACLR_ENABLED)
#include <kernel/clr/include/managed_runtime.h>
#endif

/*
 * Check whether a filename ends with ".DLL" (case-sensitive).
 * Returns 1 if it does, 0 otherwise.
 */
static int filename_ends_with_dll(const char *name)
{
    const char *p;
    uint32_t len = 0;

    if (!name)
        return 0;

    p = name;
    while (*p) {
        len++;
        p++;
    }

    if (len < 4)
        return 0;

    p = name + len - 4;
    return (p[0] == '.' &&
            (p[1] == 'D' || p[1] == 'd') &&
            (p[2] == 'L' || p[2] == 'l') &&
            (p[3] == 'L' || p[3] == 'l'));
}

/*
 * Check PE MZ header signature at the start of file data.
 * Returns 1 if the first two bytes are 'M' 'Z', 0 otherwise.
 */
static int has_mz_header(const uint8_t *data, uint32_t size)
{
    if (!data || size < 2)
        return 0;

    return (data[0] == 0x4D && data[1] == 0x5A);
}

uint32_t initramfs_load_drivers(void)
{
    uint32_t count;
    uint32_t loaded = 0;
    uint32_t i;
    ramdisk_file_t file;

    count = ramdisk_file_count();
    if (count == 0) {
        console_write("Initramfs loader   : no files in ramdisk\n");
        return 0;
    }

    console_write("Initramfs loader   : scanning ");
    console_write_dec((uint64_t)count);
    console_write(" ramdisk files\n");

    for (i = 0; i < count; i++) {
        if (ramdisk_get_file(i, &file) != RAMDISK_OK)
            continue;

        /* Skip non-DLL files */
        if (!filename_ends_with_dll(file.filename))
            continue;

        /* Skip files without valid PE MZ header */
        if (!has_mz_header(file.data, file.size))
            continue;

        console_write("  Loading          : ");
        console_write(file.filename);
        console_write(" (");
        console_write_dec((uint64_t)file.size);
        console_write(" bytes)\n");

        #if defined(ZACLR_ENABLED)
        console_write("  Deferred         : retained in ramdisk for ZACLR-owned launch path\n");
        loaded++;
        #else
        /* Attempt to load into the managed runtime assembly registry.
         * managed_runtime_load() parses the PE, registers the assembly,
         * and runs all .cctor type initializers. */
        if (managed_runtime_load(file.data, file.size)) {
            loaded++;
        } else {
            console_write("  FAILED           : ");
            console_write(file.filename);
            console_write("\n");
        }
        #endif
    }

    console_write("Initramfs loader   : loaded ");
    console_write_dec((uint64_t)loaded);
    console_write(" assemblies from ramdisk\n");

    return loaded;
}
