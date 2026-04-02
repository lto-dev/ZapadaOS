/*
 * Zapada - src/kernel/arch/aarch64/exception_vectors.S
 *
 * AArch64 EL1 exception vector table.
 *
 * The vector table must be aligned to 2 KiB (0x800) and has 16 entries,
 * each 128 bytes (32 instruction slots). The CPU dispatches to the entry
 * at offset (vector_index * 0x80) relative to VBAR_EL1.
 *
 * Table layout (ARMv8-A ARM D1.10):
 *   Index  Offset  Description
 *   -----  ------  ------------------------------------
 *   0      0x000   EL1t Synchronous  (SP_EL0 stack)
 *   1      0x080   EL1t IRQ
 *   2      0x100   EL1t FIQ
 *   3      0x180   EL1t SError
 *   4      0x200   EL1h Synchronous  (SP_EL1 stack) <- kernel sync path
 *   5      0x280   EL1h IRQ          <- Phase 2C: generic timer IRQ
 *   6      0x300   EL1h FIQ
 *   7      0x380   EL1h SError
 *   8      0x400   EL0 AArch64 Synchronous
 *   9      0x480   EL0 AArch64 IRQ
 *  10      0x500   EL0 AArch64 FIQ
 *  11      0x580   EL0 AArch64 SError
 *  12      0x600   EL0 AArch32 Synchronous
 *  13      0x680   EL0 AArch32 IRQ
 *  14      0x700   EL0 AArch32 FIQ
 *  15      0x780   EL0 AArch32 SError
 *
 * Entries 0-4 and 6-15: call aarch64_exception_handler (panics, no return).
 * Entry 5 (EL1h IRQ):   branch to el1h_irq_handler_stub (saves/restores
 *                        regs, calls aarch64_gentimer_irq_handler, ERETs).
 */

.section ".text.exception_vectors"
.balign 2048
.global exception_vectors

exception_vectors:

/*
 * vector_entry <index>
 *
 * Generates a single 128-byte aligned vector entry that:
 *   1. Reserves 48 bytes on the stack and saves x0-x5.
 *   2. Reads ESR_EL1, ELR_EL1, FAR_EL1, SPSR_EL1 into x0-x3.
 *   3. Loads the vector index into x4.
 *   4. Calls aarch64_exception_handler(esr, elr, far, spsr, index).
 *   5. Falls into a WFE spin (should never be reached).
 */
.macro vector_entry, index
    .balign 128
    sub  sp, sp, #48
    stp  x0, x1, [sp, #0]
    stp  x2, x3, [sp, #16]
    stp  x4, x5, [sp, #32]
    mrs  x0, esr_el1
    mrs  x1, elr_el1
    mrs  x2, far_el1
    mrs  x3, spsr_el1
    mov  x4, #\index
    bl   aarch64_exception_handler
1:  wfe
    b    1b
.endm

/*
 * vector_irq_entry <index>
 *
 * Generates a 128-byte aligned vector entry that branches to
 * el1h_irq_handler_stub (defined below the vector table).
 * The branch is a single instruction; the rest of the 128-byte slot
 * is padding (unreachable code).
 *
 * Used only for vector 5 (EL1h IRQ) in Phase 2C.
 */
.macro vector_irq_entry, index
    .balign 128
    b    el1h_irq_handler_stub
1:  wfe
    b    1b
.endm

/* EL1t group (SP_EL0 active when exception taken) */
vector_entry  0   /* EL1t Synchronous */
vector_entry  1   /* EL1t IRQ         */
vector_entry  2   /* EL1t FIQ         */
vector_entry  3   /* EL1t SError      */

/* EL1h group (SP_EL1 active; kernel code running) */
vector_entry     4   /* EL1h Synchronous */
vector_irq_entry 5   /* EL1h IRQ: generic timer in Phase 2C */
vector_entry     6   /* EL1h FIQ         */
vector_entry     7   /* EL1h SError      */

/* EL0 AArch64 group */
vector_entry  8   /* EL0-64 Synchronous */
vector_entry  9   /* EL0-64 IRQ         */
vector_entry 10   /* EL0-64 FIQ         */
vector_entry 11   /* EL0-64 SError      */

/* EL0 AArch32 group */
vector_entry 12   /* EL0-32 Synchronous */
vector_entry 13   /* EL0-32 IRQ         */
vector_entry 14   /* EL0-32 FIQ         */
vector_entry 15   /* EL0-32 SError      */

/*
 * el1h_irq_handler_stub
 *
 * Full GP register save/restore around the C IRQ handler.
 * Called via branch (not BL) from the vector_irq_entry slot, so
 * x30 (LR) still holds the value it had before the interrupt.
 *
 * Save frame on the stack (16-byte aligned):
 *   [sp +   0] x0,  x1
 *   [sp +  16] x2,  x3
 *   [sp +  32] x4,  x5
 *   [sp +  48] x6,  x7
 *   [sp +  64] x8,  x9
 *   [sp +  80] x10, x11
 *   [sp +  96] x12, x13
 *   [sp + 112] x14, x15
 *   [sp + 128] x16, x17
 *   [sp + 144] x18, x19
 *   [sp + 160] x20, x21
 *   [sp + 176] x22, x23
 *   [sp + 192] x24, x25
 *   [sp + 208] x26, x27
 *   [sp + 224] x28, x29 (frame pointer)
 *   [sp + 240] x30 (lr), elr_el1
 *   [sp + 256] spsr_el1, <pad>
 *   Total = 272 bytes (17 aligned pairs * 16 = 272, divisible by 16: OK)
 *
 * After the C call, restore the saved registers and ERET.
 */
.global el1h_irq_handler_stub
.extern aarch64_gentimer_irq_handler

el1h_irq_handler_stub:
    /* Allocate 272 bytes on the EL1 stack */
    sub  sp, sp, #272

    /* Save GP registers */
    stp  x0,  x1,  [sp,   #0]
    stp  x2,  x3,  [sp,  #16]
    stp  x4,  x5,  [sp,  #32]
    stp  x6,  x7,  [sp,  #48]
    stp  x8,  x9,  [sp,  #64]
    stp  x10, x11, [sp,  #80]
    stp  x12, x13, [sp,  #96]
    stp  x14, x15, [sp, #112]
    stp  x16, x17, [sp, #128]
    stp  x18, x19, [sp, #144]
    stp  x20, x21, [sp, #160]
    stp  x22, x23, [sp, #176]
    stp  x24, x25, [sp, #192]
    stp  x26, x27, [sp, #208]
    stp  x28, x29, [sp, #224]

    /* Save LR (x30) and ELR_EL1 */
    mrs  x0, elr_el1
    stp  x30, x0, [sp, #240]

    /* Save SPSR_EL1 (and a zero pad to keep 16-byte alignment) */
    mrs  x0, spsr_el1
    stp  x0, xzr, [sp, #256]

    /* Call the C IRQ handler */
    bl   aarch64_gentimer_irq_handler

    /* Restore SPSR_EL1 */
    ldp  x0, xzr, [sp, #256]
    msr  spsr_el1, x0

    /* Restore ELR_EL1 and LR */
    ldp  x30, x0, [sp, #240]
    msr  elr_el1, x0

    /* Restore GP registers */
    ldp  x28, x29, [sp, #224]
    ldp  x26, x27, [sp, #208]
    ldp  x24, x25, [sp, #192]
    ldp  x22, x23, [sp, #176]
    ldp  x20, x21, [sp, #160]
    ldp  x18, x19, [sp, #144]
    ldp  x16, x17, [sp, #128]
    ldp  x14, x15, [sp, #112]
    ldp  x12, x13, [sp,  #96]
    ldp  x10, x11, [sp,  #80]
    ldp  x8,  x9,  [sp,  #64]
    ldp  x6,  x7,  [sp,  #48]
    ldp  x4,  x5,  [sp,  #32]
    ldp  x2,  x3,  [sp,  #16]
    ldp  x0,  x1,  [sp,   #0]

    /* Deallocate frame */
    add  sp, sp, #272

    /* Return to interrupted code */
    eret

