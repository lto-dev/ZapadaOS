/*
 * Zapada - src/boot/aarch64/boot.S
 *
 * AArch64 bare-metal entry point for Raspberry Pi 3/4/5.
 *
 * The RPi firmware (and QEMU -M raspi3b) loads kernel8.img to physical
 * address 0x80000 and jumps to _start.
 *
 * On entry:
 *   x0        = FDT physical address (passed by firmware; may be 0).
 *   CurrentEL = EL3 (QEMU raspi3b default) or EL2 (some firmware configs).
 *
 * This stub:
 *   1. Parks CPUs 1-3 in a WFE spin loop.
 *   2. Reads CurrentEL and drops to EL1h through each required level:
 *        EL3 -> EL2 -> EL1h
 *        EL2 -> EL1h
 *        EL1 -> (no drop needed)
 *   3. Disables the MMU in SCTLR_EL1 (ensure physical addressing).
 *   4. Sets up the EL1 stack below _start (physical 0x80000).
 *   5. Clears the BSS segment.
 *   6. Calls kernel_main_aarch64(fdt_base).
 *
 * There is no return from kernel_main_aarch64. A WFE spin loop guards
 * against accidental returns.
 */

.section ".text.boot"
.global _start

.section ".bss"
.balign 16
.global g_boot_stack
g_boot_stack:
    .skip 65536
.global g_boot_stack_top
g_boot_stack_top:

.section ".text.boot"

_start:
    /* Save the FDT pointer (x0) into x20 (callee-saved) before any
     * register is clobbered by the EL switch sequence. */
    mov x20, x0

    /* Park all secondary CPUs (Aff0 != 0 means not CPU 0). */
    mrs x0, mpidr_el1
    and x0, x0, #0xFF
    cbz x0, .Lprimary_cpu

.Lspin:
    wfe
    b   .Lspin

.Lprimary_cpu:
    /* Read CurrentEL and dispatch to the appropriate drop path. */
    mrs x0, CurrentEL
    lsr x0, x0, #2           /* EL field is bits [3:2]; shift to [1:0] */
    and x0, x0, #3

    cmp x0, #3
    beq .Ldrop_el3_to_el2    /* QEMU raspi3b default: boots at EL3 */
    cmp x0, #2
    beq .Ldrop_el2_to_el1
    b   .Lsetup_el1           /* Already at EL1 */

/* ---------------------------------------------------------------------- */
/* Drop from EL3 to EL2                                                   */
/* ---------------------------------------------------------------------- */

.Ldrop_el3_to_el2:
    /*
     * SCR_EL3 (Secure Configuration Register):
     *   NS  (bit  0) = 1 : Non-secure state for EL2/EL1/EL0.
     *   SMD (bit  7) = 1 : SMC instruction disabled at EL1 and below.
     *   HCE (bit  8) = 1 : HVC instruction enabled at EL2.
     *   RW  (bit 10) = 1 : EL2 and below execute in AArch64 mode.
     *
     * Bits: 0x400 | 0x100 | 0x080 | 0x001 = 0x581
     */
    mov x0, #0x581
    msr scr_el3, x0

    /* CPTR_EL3 = 0: no trapping of FP/SIMD at EL3. */
    msr cptr_el3, xzr

    /*
     * SPSR_EL3: return to EL2h, all DAIF interrupts masked.
     *   M[3:0] = 0b1001 (EL2h)
     *   DAIF   = 0b1111 at bits [9:6]  -> 0x3C0
     *   Total  = 0x3C9
     */
    mov x0, #0x3C9
    msr spsr_el3, x0

    /* ELR_EL3: resume execution at the EL2->EL1 drop path after eret. */
    adr x0, .Ldrop_el2_to_el1
    msr elr_el3, x0

    eret

/* ---------------------------------------------------------------------- */
/* Drop from EL2 to EL1                                                   */
/* ---------------------------------------------------------------------- */

.Ldrop_el2_to_el1:
    /*
     * HCR_EL2 (Hypervisor Configuration Register):
     *   RW (bit 31) = 1 : EL1 executes in AArch64 mode.
     *   No virtualization features enabled.
     */
    mov x0, #(1 << 31)
    msr hcr_el2, x0

    /*
     * Set SCTLR_EL1 to a safe reset-like value.
     * Required RES1 bits per ARMv8-A ARM: 29, 28, 23, 22, 20, 11.
     * MMU (bit 0), D-cache (bit 2), I-cache (bit 12) are left as 0.
     *
     * Encoded: 0x30D00800
     *   bit 29 = 0x20000000
     *   bit 28 = 0x10000000
     *   bit 23 = 0x00800000
     *   bit 22 = 0x00400000
     *   bit 20 = 0x00100000
     *   bit 11 = 0x00000800
     * Combined: 0x30D00800 -> movk encoding: high half = 0x30D0, low = 0x0800
     */
    mov x0, #0x0800
    movk x0, #0x30D0, lsl #16
    msr sctlr_el1, x0

    /*
     * SPSR_EL2: return to EL1h, all DAIF interrupts masked.
     *   M[3:0] = 0b0101 (EL1h)
     *   DAIF   = 0b1111 at bits [9:6] -> 0x3C0
     *   Total  = 0x3C5
     */
    mov x0, #0x3C5
    msr spsr_el2, x0

    /* ELR_EL2: resume at .Lsetup_el1 after eret. */
    adr x0, .Lsetup_el1
    msr elr_el2, x0

    eret

/* ---------------------------------------------------------------------- */
/* EL1 setup: MMU off, stack, BSS clear, call C entry                    */
/* ---------------------------------------------------------------------- */

.Lsetup_el1:
    isb

    /* Disable MMU in SCTLR_EL1 to guarantee physical addressing. */
    mrs x0, sctlr_el1
    bic x0, x0, #1
    msr sctlr_el1, x0
    isb

    /*
     * Set up the EL1 stack using a dedicated static boot stack rather than
     * assuming the linked image starts at a safe RAM address.
     */
    ldr x0, =g_boot_stack_top
    mov sp, x0

    /* Clear BSS to zero (8 bytes per iteration). */
    ldr x0, =__bss_start
    ldr x1, =__bss_end
.Lclear_bss:
    cmp x0, x1
    bge .Ldone_bss
    str xzr, [x0], #8
    b   .Lclear_bss
.Ldone_bss:

    /* Call the C kernel entry point, passing the saved FDT address. */
    mov x0, x20
    bl  kernel_main_aarch64

    /* kernel_main_aarch64 must not return. Guard with a WFE spin. */
.Lhalt:
    wfe
    b   .Lhalt

