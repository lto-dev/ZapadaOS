/*
 * Zapada - src/kernel/arch/aarch64/context_switch.S
 *
 * Phase 2C: Kernel-mode cooperative context switch for AArch64.
 *
 * Convention (AAPCS64):
 *   sched_aa64_context_switch(cpu_context_t *prev, cpu_context_t *next)
 *     x0 = prev  (pointer to current thread's cpu_context_t)
 *     x1 = next  (pointer to next thread's cpu_context_t)
 *
 * Layout of cpu_context_t (see process.h):
 *   offset 0:  sp    (uint64_t) - saved kernel SP
 *   offset 8:  ip    (uint64_t) - initial entry point (for first dispatch)
 *   offset 16: flags (uint64_t) - saved flags (informational)
 *
 * Algorithm:
 *   1. Push callee-saved registers (AAPCS64: x19-x28, x29 (FP), x30 (LR))
 *      onto the CURRENT thread's kernel stack.
 *   2. Save current SP to prev->sp (offset 0 of cpu_context_t).
 *   3. Load SP from next->sp (offset 0 of cpu_context_t).
 *   4. Pop callee-saved registers from the NEXT thread's kernel stack.
 *   5. RET: the LR on the new stack is either:
 *      a. The return address from the call to sched_aa64_context_switch
 *         (resume path for a thread that previously yielded).
 *      b. A thread_entry_trampoline address set up by kstack_init_context
 *         for a brand-new thread that has never run.
 *
 * Callee-saved registers (AAPCS64):
 *   x19-x28, x29 (frame pointer), x30 (link register / return address)
 *   sp is handled explicitly.
 *
 * AArch64 alignment note:
 *   The stack pointer must be 16-byte aligned at all times.
 *   We push 11 pairs of registers (10 pairs x19-x28, plus x29/x30),
 *   which is 11 * 16 = 176 bytes. Stack alignment is preserved because
 *   176 is a multiple of 16.
 */

.section ".text"
.global sched_aa64_context_switch
.global thread_trampoline_aa64

sched_aa64_context_switch:
    /* Push callee-saved registers onto the CURRENT thread's kernel stack.
     * Push order: x19/x20, x21/x22, x23/x24, x25/x26, x27/x28, x29/x30.
     * Pop order (below) is reversed.
     * Uses pre-decrement addressing (stack grows downward). */
    stp  x19, x20, [sp, #-16]!
    stp  x21, x22, [sp, #-16]!
    stp  x23, x24, [sp, #-16]!
    stp  x25, x26, [sp, #-16]!
    stp  x27, x28, [sp, #-16]!
    stp  x29, x30, [sp, #-16]!

    /* Save current SP to prev->sp (x0 = prev, offset 0) */
    mov  x2, sp
    str  x2, [x0, #0]

    /* Load next thread's SP from next->sp (x1 = next, offset 0) */
    ldr  x2, [x1, #0]
    mov  sp, x2

    /* Pop callee-saved registers from the NEXT thread's stack.
     * Order is reversed (post-increment). */
    ldp  x29, x30, [sp], #16
    ldp  x27, x28, [sp], #16
    ldp  x25, x26, [sp], #16
    ldp  x23, x24, [sp], #16
    ldp  x21, x22, [sp], #16
    ldp  x19, x20, [sp], #16

    /* Return to the resume point (x30 = LR from the popped frame).
     * For a previously-running thread: returns to caller of sched_aa64_context_switch.
     * For a new thread: returns to thread_trampoline_aa64. */
    ret

/* -------------------------------------------------------------------------
 * thread_trampoline_aa64
 *
 * Entry point for new kernel threads on their first dispatch.
 *
 * kstack_init_context places the thread's actual entry function address in
 * x19 (callee-saved, restored by sched_aa64_context_switch). This trampoline
 * calls that function. If the thread function ever returns, the trampoline
 * loops forever (no valid return path for kernel threads in Phase 2C).
 * -------------------------------------------------------------------------
 */
thread_trampoline_aa64:
    /* x19 = entry_fn (restored by sched_aa64_context_switch) */
    blr  x19

    /* Thread entry function returned unexpectedly. Spin forever. */
1:
    wfe
    b    1b

