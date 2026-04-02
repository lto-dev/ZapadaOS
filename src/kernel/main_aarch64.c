/*
 * Zapada - src/kernel/main_aarch64.c
 *
 * AArch64 kernel entry point.
 *
 * kernel_main_aarch64 is called from src/boot/aarch64/boot.S after:
 *   - Secondary CPUs are parked.
 *   - EL3 -> EL2 -> EL1h drop complete.
 *   - EL1 stack set up at 0x80000.
 *   - BSS cleared.
 *
 * Parameter:
 *   fdt_base - physical address of the Flattened Device Tree provided
 *              by the RPi firmware (or QEMU). May be 0 if no FDT.
 *
 * Stage A.1 - PL011 UART console, EL confirmation, halt.
 * Stage A.2 - Exception vectors (VBAR_EL1), FDT memory probe,
 *             deliberate BRK exception to validate the handler path.
 * Stage A.3 - Console abstraction introduced. PMM initialized from
 *             FDT or hardcoded RPi 3 fallback. Bump heap initialized.
 *             All shared subsystems (pmm, heap) use console.h for output.
 * Managed runtime substrate - verifier + expanded CIL interpreter.
 *                             Arithmetic, locals, branches, managed boot handoff.
 * Phase 2B  - Process model, scheduler foundation, timer stub, syscall
 *             dispatch, IPC v1 bounded channels. Self-tests on all paths.
 * Phase 2C  - Generic timer (CNTP_EL0), kernel stack pool, real context
 *             switch (AArch64 callee-save frame), blocking IPC, GDT analogue,
 *             scheduler policy syscalls. Self-tests on all paths.
 * Current managed boundary - VirtIO block HAL in native code, managed boot and
 *                            managed storage/VFS above it.
 */

#include <kernel/arch/aarch64/uart.h>
#include <kernel/arch/aarch64/exception.h>
#include <kernel/arch/aarch64/fdt.h>
#include <kernel/drivers/virtio_blk.h>
#include <kernel/gates/phase_gates.h>
#include <kernel/console.h>
#include <kernel/initramfs/bootstrap.h>
#include <kernel/initramfs/loader.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/mm_defs.h>
#include <kernel/mm/pmm.h>
#include <kernel/phase2b.h>
#include <kernel/phase2c.h>
#include <kernel/phase32_step1.h>
#include <kernel/types.h>

#if defined(ZACLR_ENABLED)
#include <kernel/zaclr/boot/zaclr_boot_path.h>
#else
#include <kernel/clr/api/runtime_api.h>
#include <kernel/clr/runtime/runtime_boot.h>
#include <kernel/clr/host/runtime_kernel_host.h>
#endif

/*
 * RPi 3 fallback memory region constants are defined in fdt.h:
 *   RPI3_RAM_BASE  0x00000000UL
 *   RPI3_RAM_SIZE  0x40000000UL  (1 GiB)
 * They are used here as the fallback when no FDT is present or when
 * the FDT /memory node is not found.
 */

/* ------------------------------------------------------------------ */
/* Helper: print current exception level                               */
/* ------------------------------------------------------------------ */

static void aarch64_print_el(void)
{
    uint64_t el;
    __asm__ volatile ("mrs %0, CurrentEL" : "=r"(el));
    el = (el >> 2) & 3u;
    console_write("Exception level    : EL");
    console_write_dec(el);
    console_write("\n");
}

/* ------------------------------------------------------------------ */
/* Helper: probe FDT and return the detected memory region             */
/*                                                                     */
/* On success, *out_base and *out_size are set from the FDT /memory    */
/* node. On failure or absent FDT, the RPi 3 hardcoded fallback is    */
/* used. The caller must initialize *out_base and *out_size to the     */
/* fallback values before calling this function.                       */
/* ------------------------------------------------------------------ */

static void probe_fdt(uint64_t fdt_base, uint64_t *out_base, uint64_t *out_size)
{
    console_write("FDT base           : ");
    console_write_hex64(fdt_base);
    console_write("\n");

    if (fdt_is_valid(fdt_base)) {
        uint64_t mem_base = RPI3_RAM_BASE;
        uint64_t mem_size = RPI3_RAM_SIZE;

        console_write("FDT                : valid\n");

        if (fdt_get_memory(fdt_base, &mem_base, &mem_size)) {
            console_write("FDT memory base    : ");
            console_write_hex64(mem_base);
            console_write("\n");
            console_write("FDT memory size    : ");
            console_write_hex64(mem_size);
            console_write("\n");
            *out_base = mem_base;
            *out_size = mem_size;
        } else {
            console_write("FDT memory         : /memory node not found\n");
            console_write("Memory fallback    : 1 GiB at 0x00000000 (BCM2837 layout)\n");
            /* *out_base and *out_size remain at fallback values from caller */
        }
    } else {
        console_write("FDT                : not present (QEMU default)\n");
        console_write("Memory fallback    : 1 GiB at 0x00000000 (BCM2837 layout)\n");
        /* *out_base and *out_size remain at fallback values from caller */
    }
}

static void probe_aarch64_display(uint64_t fdt_base)
{
    (void)fdt_base;
    console_write("Display            : no framebuffer handoff implemented on AArch64 yet\n");
    console_write("Display path       : serial/UART remains primary on this architecture\n");
}

/* ------------------------------------------------------------------ */
/* kernel_main_aarch64                                                  */
/* ------------------------------------------------------------------ */

void kernel_main_aarch64(uint64_t fdt_base)
{
    uint64_t mem_base = RPI3_RAM_BASE;
    uint64_t mem_size = RPI3_RAM_SIZE;
    uint64_t initrd_start = 0u;
    uint64_t initrd_end = 0u;
    void    *heap_probe;
    uint64_t heap_reservation_bytes;

    /*
     * Stage A.1: bring the PL011 UART online.
     * After this point all console_* calls delegate to uart_write /
     * uart_write_hex64 / uart_write_dec via src/kernel/arch/aarch64/console.c.
     */
    uart_init();

    console_write("\n");
    console_write("Zapada - Phase 3A AArch64 bring-up\n");
    console_write("---------------------------------\n");

    aarch64_print_el();

    /*
     * Stage A.2: install the exception vector table before anything
     * that could fault. VBAR_EL1 is written and ISB fences the
     * pipeline so the new table is visible immediately.
     */
    exception_init();
    console_write("VBAR_EL1           : exception vectors installed\n");

    /* Probe the FDT for available RAM. Populates mem_base and mem_size. */
    probe_fdt(fdt_base, &mem_base, &mem_size);
    probe_aarch64_display(fdt_base);

    if (fdt_get_initrd(fdt_base, &initrd_start, &initrd_end)) {
        console_write("Initramfs module   : discovered\n");
        console_write("  Start            : ");
        console_write_hex64(initrd_start);
        console_write("\n");
        console_write("  Size             : ");
        console_write_dec(initrd_end - initrd_start);
        console_write(" bytes\n");
    } else {
        console_write("Initramfs module   : not provided by firmware/FDT\n");
    }

    heap_reservation_bytes = (uint64_t)initramfs_required_heap_bytes(
        (const uint8_t *)(uintptr_t)initrd_start,
        (uint32_t)(initrd_end - initrd_start));
    if (heap_reservation_bytes < (uint64_t)EARLY_HEAP_SIZE) {
        heap_reservation_bytes = (uint64_t)EARLY_HEAP_SIZE;
    }

    pmm_reserve_until((uint64_t)(uintptr_t)&kernel_end + heap_reservation_bytes);

    /*
     * Stage A.3: initialize the physical frame allocator from the
     * memory region reported by the FDT (or the RPi 3 fallback).
     * Frames within the kernel image and bump heap reservation are
     * skipped by pmm_init_range (see pmm.c and mm_defs.h).
     */
    pmm_init_range(mem_base, mem_size);
    console_write("PMM free frames    : ");
    console_write_dec(pmm_get_free_frame_count());
    console_write("\n");

    /*
     * Stage A.3: initialize the early bump heap immediately above the
     * kernel image.
     */
    kheap_set_size((size_t)heap_reservation_bytes);
    kheap_init();
    heap_probe = kheap_alloc(1);
    console_write("Heap init          : OK\n");
    if (heap_probe != NULL) {
        console_write("Heap probe         : ");
        console_write_hex64((uint64_t)(uintptr_t)heap_probe);
        console_write("\n");
    }
    console_write("Heap free bytes    : ");
    console_write_dec((uint64_t)kheap_get_free_bytes());
    console_write("\n");

    initramfs_bootstrap((const uint8_t *)(uintptr_t)initrd_start,
                        (uint32_t)(initrd_end - initrd_start));

    console_write("\n");
    console_write("Phase 2A check     :\n");
    console_write("  - Boot stub      : EL3->EL2->EL1 drop complete\n");
    console_write("  - Exception vecs : VBAR_EL1 set\n");
    console_write("  - FDT            : probed\n");
    console_write("  - PMM            : initialized\n");
    console_write("  - Heap           : initialized\n");
    console_write("\n");

    initramfs_load_drivers();

    /*
     * Foundation bring-up.
     * phase2b/phase2c still own their current internal self-tests, but boot
     * now presents unified gates through phase_gates_run().
     */
    phase2b_init();

    /*
     * Foundation follow-up bring-up.
     * Keep IRQs masked through Phase 2B self-tests for parity with x86_64.
     * Unmask here, immediately before the Phase 2C bring-up/tests.
     */
    __asm__ volatile ("msr daifclr, #2");
    __asm__ volatile ("isb");
    phase2c_init();

    /*
     * Probe and initialize the VirtIO block device.
     * Must be called before the managed boot payload so that
     * Zapada.BlockDev.ReadSector InternalCalls succeed.
     */
    (void)virtio_blk_probe_and_init();

    phase_gates_run();

    #if defined(ZACLR_ENABLED)
    console_write("ZACLR boot path    : enabled\n");

    switch (zaclr_boot_kernel_entry()) {
        case ZACLR_KERNEL_BOOT_STATUS_OK:
            console_write("ZACLR boot path    : completed\n");
            break;
        case ZACLR_KERNEL_BOOT_STATUS_BOOTSTRAP_FAILED:
            console_write("ZACLR boot path    : bootstrap failed\n");
            for (;;) {
                __asm__ volatile ("wfe");
            }
        case ZACLR_KERNEL_BOOT_STATUS_LAUNCH_FAILED:
        default:
            console_write("ZACLR boot path    : launch failed\n");
            for (;;) {
                __asm__ volatile ("wfe");
            }
    }

    console_write("\n");
    console_write("System halted after ZACLR boot validation path.\n");
    #else
    if (!zapada_runtime_host_register_kernel_services()) {
        console_write("CLR host           : kernel service registration failed\n");
        for (;;) {
            __asm__ volatile ("wfe");
        }
    }

    console_write("CLR host           : kernel services registered\n");

    switch (zapada_clr_runtime_initialize()) {
        case ZAPADA_CLR_RUNTIME_STATUS_OK:
            console_write("CLR runtime        : API initialized\n");
            break;
        case ZAPADA_CLR_RUNTIME_STATUS_NOT_IMPLEMENTED:
            console_write("CLR runtime        : API scaffold active\n");
            break;
        default:
            console_write("CLR runtime        : API initialization failed\n");
            for (;;) {
                __asm__ volatile ("wfe");
            }
    }

    if (!zapada_clr_boot()) {
        console_write("CLR host           : replacement runtime bootstrap failed\n");
        for (;;) {
            __asm__ volatile ("wfe");
        }
    }

    console_write("CLR host           : replacement runtime bootstrap OK\n");

    console_write("\n");
    console_write("System halted after successful runtime scaffold bring-up.\n");
    #endif

    for (;;) {
        __asm__ volatile ("wfe");
    }
}


