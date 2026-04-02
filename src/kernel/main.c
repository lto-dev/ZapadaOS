/*
 * Zapada - src/kernel/main.c
 *
 * Native kernel entry point.
 *
 * kernel_main is called from the 64-bit long mode assembly entry in
 * src/boot/boot.asm after page tables, GDT, and the initial stack are set up.
 *
 * Responsibilities at the current boot baseline:
 *   - Initialize the serial console (only diagnostic channel).
 *   - Validate the Multiboot2 magic value.
 *   - Print boot banner and Multiboot2 information.
 *   - Print the physical memory map.
 *   - Load the runtime GDT (now includes ring-3 code/data descriptors).
 *   - Load the IDT (exception vectors 0-31, IRQ stubs 0x20+, default 32-255).
 *   - Initialize the physical frame allocator from the memory map.
 *   - Initialize the early bump heap.
 *   - Execute the managed boot entry after native substrate bring-up.
 *   - Initialize Phase 2B subsystems.
 *   - Enable hardware interrupts (STI).
 *   - Initialize Phase 2C subsystems: PIC+PIT, kernel stack pool, real
 *     context switch, blocking IPC, user-mode GDT groundwork, scheduler
 *     policy syscalls. Run Phase 2C self-tests.
 *   - Initialize the permanent native boundary needed by managed storage and VFS.
 *   - Halt.
 *
 * This function must not call any function that depends on memory allocation,
 * GC, or any service not yet initialized.
 *
 * Parameters (from boot.asm via x86-64 SysV ABI):
 *   mb_magic - Multiboot2 bootloader magic (should be MULTIBOOT2_BOOTLOADER_MAGIC)
 *   mb_info  - Physical address of the Multiboot2 information structure
 */

#include <boot/multiboot2.h>
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/drivers/virtio_blk.h>
#include <kernel/fb_console.h>
#include <kernel/gates/phase_gates.h>
#include <kernel/initramfs/bootstrap.h>
#include <kernel/initramfs/loader.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/mm_defs.h>
#include <kernel/mm/pmm.h>
#include <kernel/panic.h>
#include <kernel/phase2b.h>
#include <kernel/phase2c.h>
#include <kernel/phase32_step1.h>
#include <kernel/serial.h>
#include <kernel/text_console.h>
#include <kernel/types.h>

#if defined(ZACLR_ENABLED)
#include <kernel/zaclr/boot/zaclr_boot_path.h>
#else
#include <kernel/clr/api/runtime_api.h>
#include <kernel/clr/runtime/runtime_boot.h>
#include <kernel/clr/host/runtime_kernel_host.h>
#endif

/* --------------------------------------------------------------------------
 * Static boot-time reporting helpers.
 * These keep kernel_main focused on sequenced initialization steps.
 * -------------------------------------------------------------------------- */

static void boot_print_banner(void)
{
    serial_write("\n");
    serial_write("Zapada - Phase 3A bring-up\n");
    serial_write("-------------------------\n");
}

static void boot_print_mb2_info(mb2_info_t *mb_info)
{
    serial_write("Multiboot2 magic   : OK\n");
    serial_write("Multiboot2 info    : ");
    serial_write_hex64((uint64_t)(uintptr_t)mb_info);
    serial_write("\n");
    serial_write("Multiboot2 size    : ");
    serial_write_dec((uint64_t)mb_info->total_size);
    serial_write(" bytes\n");
}

static void boot_print_memory_map(mb2_info_t *mb_info)
{
    mb2_tag_mmap_t   *mmap_tag;
    mb2_mmap_entry_t *entry;
    uint32_t          n_entries;
    uint32_t          i;

    mmap_tag = (mb2_tag_mmap_t *)mb2_find_tag(mb_info, MB2_TAG_MEMORY_MAP);
    if (mmap_tag == NULL) {
        serial_write("Memory map         : not provided by bootloader\n");
        return;
    }

    n_entries = (mmap_tag->size - sizeof(mb2_tag_mmap_t)) / mmap_tag->entry_size;
    entry     = (mb2_mmap_entry_t *)((uint8_t *)mmap_tag + sizeof(mb2_tag_mmap_t));

    serial_write("Memory map         : ");
    serial_write_dec((uint64_t)n_entries);
    serial_write(" entries\n");

    for (i = 0; i < n_entries; i++) {
        serial_write("  [");
        serial_write_dec((uint64_t)i);
        serial_write("] base=");
        serial_write_hex64(entry->base_addr);
        serial_write(" len=");
        serial_write_hex64(entry->length);
        serial_write(" type=");
        serial_write_dec((uint64_t)entry->type);
        serial_write(entry->type == MB2_MEMORY_AVAILABLE ? " (available)" : " (reserved)");
        serial_write("\n");

        entry = (mb2_mmap_entry_t *)((uint8_t *)entry + mmap_tag->entry_size);
    }
}

static void boot_print_bootloader_name(mb2_info_t *mb_info)
{
    mb2_tag_loader_name_t *loader_tag;

    loader_tag = (mb2_tag_loader_name_t *)mb2_find_tag(mb_info,
                                                        MB2_TAG_BOOT_LOADER_NAME);
    if (loader_tag != NULL) {
        serial_write("Bootloader         : ");
        serial_write(loader_tag->string);
        serial_write("\n");
    }
}

static void boot_try_init_framebuffer(mb2_info_t *mb_info)
{
    mb2_tag_framebuffer_rgb_t *fb_tag;
    fb_console_info_t          fb_info;
    text_console_info_t        text_info;

    fb_tag = (mb2_tag_framebuffer_rgb_t *)mb2_find_tag(mb_info, MB2_TAG_FRAMEBUFFER);
    if (fb_tag == NULL) {
        serial_write("Framebuffer        : not provided by bootloader\n");
        return;
    }

    if (fb_tag->common.framebuffer_type != 1u) {
        serial_write("Framebuffer type   : ");
        serial_write_dec((uint64_t)fb_tag->common.framebuffer_type);
        serial_write("\n");

        if (fb_tag->common.framebuffer_type == 2u) {
            text_info.buffer_addr = (uintptr_t)fb_tag->common.framebuffer_addr;
            text_info.cols        = fb_tag->common.framebuffer_width;
            text_info.rows        = fb_tag->common.framebuffer_height;
            text_console_init(&text_info);
            if (text_console_is_ready()) {
                serial_write("Text console       : initialized\n");
                return;
            }
        }

        serial_write("Framebuffer        : unsupported type\n");
        return;
    }

    fb_info.fb_addr         = (uintptr_t)fb_tag->common.framebuffer_addr;
    fb_info.width           = fb_tag->common.framebuffer_width;
    fb_info.height          = fb_tag->common.framebuffer_height;
    fb_info.pitch           = fb_tag->common.framebuffer_pitch;
    fb_info.bpp             = fb_tag->common.framebuffer_bpp;
    fb_info.red_pos         = fb_tag->framebuffer_red_field_position;
    fb_info.red_mask_size   = fb_tag->framebuffer_red_mask_size;
    fb_info.green_pos       = fb_tag->framebuffer_green_field_position;
    fb_info.green_mask_size = fb_tag->framebuffer_green_mask_size;
    fb_info.blue_pos        = fb_tag->framebuffer_blue_field_position;
    fb_info.blue_mask_size  = fb_tag->framebuffer_blue_mask_size;

    fb_console_init(&fb_info);
    if (fb_console_is_ready()) {
        serial_write("Framebuffer        : initialized\n");
    } else {
        serial_write("Framebuffer        : unsupported mode\n");
    }
}

/* --------------------------------------------------------------------------
 * Phase 2 initramfs discovery support
 * -------------------------------------------------------------------------- */

/* Global storage for discovered initramfs module. */
static uint32_t g_initramfs_start = 0;
static uint32_t g_initramfs_size  = 0;

static uintptr_t boot_get_heap_floor(void)
{
    uintptr_t heap_floor = mm_align_up_uintptr((uintptr_t)&kernel_end, 4096u);

    if (g_initramfs_size != 0u) {
        uintptr_t module_end = mm_align_up_uintptr((uintptr_t)g_initramfs_start +
                                                   (uintptr_t)g_initramfs_size,
                                                   4096u);
        if (module_end > heap_floor) {
            heap_floor = module_end;
        }
    }

    return heap_floor;
}

static uint64_t boot_get_heap_reservation_bytes(void)
{
    uint32_t initramfs_extra = initramfs_required_heap_bytes(
        (const uint8_t *)(uintptr_t)g_initramfs_start,
        g_initramfs_size);

    if ((uint64_t)initramfs_extra > (uint64_t)EARLY_HEAP_SIZE) {
        return (uint64_t)initramfs_extra;
    }

    return (uint64_t)EARLY_HEAP_SIZE;
}

static void boot_discover_initramfs(mb2_info_t *mb_info)
{
    mb2_tag_module_t *module_tag;

    module_tag = (mb2_tag_module_t *)mb2_find_tag(mb_info, MB2_TAG_MODULE);
    if (module_tag == NULL) {
        serial_write("Initramfs module   : not provided by bootloader\n");
        return;
    }

    g_initramfs_start = module_tag->mod_start;
    g_initramfs_size  = module_tag->mod_end - module_tag->mod_start;

    serial_write("Initramfs module   : discovered\n");
    serial_write("  Start            : 0x");
    serial_write_hex64((uint64_t)g_initramfs_start);
    serial_write("\n");
    serial_write("  Size             : ");
    serial_write_dec((uint64_t)g_initramfs_size);
    serial_write(" bytes\n");
}

/* --------------------------------------------------------------------------
 * kernel_main - primary C entry point
 * -------------------------------------------------------------------------- */

void kernel_main(uint32_t mb_magic, mb2_info_t *mb_info)
{
    void *heap_probe;

    /* Serial must come first - it is the only diagnostic channel. */
    if (!serial_init()) {
        /*
         * No serial output available. We cannot communicate the failure.
         * Halt silently. In practice QEMU always provides COM1, so this
         * branch indicates a configuration problem.
         */
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    /*
     * Disable interrupts defensively. boot.asm never enables them, but
     * this makes the invariant explicit and guards against future changes
     * that might inadvertently enable interrupts before the IDT is ready.
     */
    __asm__ volatile ("cli");

    boot_print_banner();

    /* Verify the Multiboot2 magic value. */
    if (mb_magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        serial_write("Boot error: invalid Multiboot2 magic: ");
        serial_write_hex64((uint64_t)mb_magic);
        serial_write("\n");
        PANIC("Invalid Multiboot2 magic. Was the kernel loaded by a "
              "Multiboot2-compliant bootloader?");
    }

    boot_print_mb2_info(mb_info);
    boot_print_memory_map(mb_info);
    boot_print_bootloader_name(mb_info);
    boot_discover_initramfs(mb_info);
    boot_try_init_framebuffer(mb_info);

    gdt_init();
    serial_write("GDT                : loaded\n");

    idt_init();
    serial_write("IDT                : loaded (vectors 0-255 installed)\n");

    pmm_reserve_until(boot_get_heap_floor() + boot_get_heap_reservation_bytes());
    pmm_init(mb_info);
    serial_write("PMM free frames    : ");
    serial_write_dec(pmm_get_free_frame_count());
    serial_write("\n");

    kheap_set_size((size_t)boot_get_heap_reservation_bytes());
    kheap_reserve_until(boot_get_heap_floor());
    kheap_init();
    heap_probe = kheap_alloc(64);
    serial_write("Heap probe         : ");
    if (heap_probe != NULL) {
        serial_write_hex64((uint64_t)(uintptr_t)heap_probe);
        serial_write("\n");
        serial_write("Heap free bytes    : ");
        serial_write_dec((uint64_t)kheap_get_free_bytes());
        serial_write("\n");
    } else {
        serial_write("FAILED\n");
    }

    initramfs_bootstrap((const uint8_t *)(uintptr_t)g_initramfs_start,
                        g_initramfs_size);

    serial_write("\n");
    serial_write("Phase 2A check     : native scaffolding initialized\n");
    serial_write("  - Serial console  : operational\n");
    serial_write("  - Long mode       : active (we are executing 64-bit code)\n");
    serial_write("  - Multiboot2 info : readable\n");
    serial_write("  - Memory map      : parsed\n");
    serial_write("  - GDT             : loaded\n");
    serial_write("  - IDT             : loaded\n");
    serial_write("  - PMM             : initialized\n");
    serial_write("  - Heap            : initialized\n");
    serial_write("\n");

    initramfs_load_drivers();

    /*
     * Foundation bring-up.
     * phase2b/phase2c still own their current internal self-tests, but boot
     * now presents unified gates through phase_gates_run().
     */
    phase2b_init();

    /*
     * Enable hardware interrupts now that:
     *   - The IDT is loaded (IRQ stubs at vectors 0x20-0x27 are in place).
     *   - Phase 2B scheduler state is initialized.
     * The PIT will be programmed by phase2c_init -> timer_init -> pit_init.
     * IRQ0 fires only after pit_init() unmasks it in the PIC.
     * STI here is safe: no handler can fire before the PIT is programmed.
     */
    __asm__ volatile ("sti");

    /* Foundation follow-up bring-up. */
    phase2c_init();

    /*
     * Probe and initialize the VirtIO block device.
     * Must be called before the managed boot payload so that
     * Zapada.BlockDev.ReadSector InternalCalls succeed.
     */
    (void)virtio_blk_probe_and_init();

    phase_gates_run();

    #if defined(ZACLR_ENABLED)
    serial_write("ZACLR boot path    : enabled\n");

    switch (zaclr_boot_kernel_entry()) {
        case ZACLR_KERNEL_BOOT_STATUS_OK:
            serial_write("ZACLR boot path    : completed\n");
            break;
        case ZACLR_KERNEL_BOOT_STATUS_BOOTSTRAP_FAILED:
            serial_write("ZACLR boot path    : bootstrap failed\n");
            __asm__ volatile ("cli");
            for (;;) {
                __asm__ volatile ("hlt");
            }
        case ZACLR_KERNEL_BOOT_STATUS_LAUNCH_FAILED:
        default:
            serial_write("ZACLR boot path    : launch failed\n");
            __asm__ volatile ("cli");
            for (;;) {
                __asm__ volatile ("hlt");
            }
    }

    serial_write("\n");
    serial_write("System halted after ZACLR boot validation path.\n");
    #else
    if (!zapada_runtime_host_register_kernel_services()) {
        serial_write("CLR host           : kernel service registration failed\n");
        __asm__ volatile ("cli");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    serial_write("CLR host           : kernel services registered\n");

    switch (zapada_clr_runtime_initialize()) {
        case ZAPADA_CLR_RUNTIME_STATUS_OK:
            serial_write("CLR runtime        : API initialized\n");
            break;
        case ZAPADA_CLR_RUNTIME_STATUS_NOT_IMPLEMENTED:
            serial_write("CLR runtime        : API scaffold active\n");
            break;
        default:
            serial_write("CLR runtime        : API initialization failed\n");
            __asm__ volatile ("cli");
            for (;;) {
                __asm__ volatile ("hlt");
            }
    }

    if (!zapada_clr_boot()) {
        serial_write("CLR host           : replacement runtime bootstrap failed\n");
        __asm__ volatile ("cli");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    serial_write("CLR host           : replacement runtime bootstrap OK\n");

    serial_write("\n");
    serial_write("System halted after successful runtime scaffold bring-up.\n");
    #endif

    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}


