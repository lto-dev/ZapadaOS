/*
 * Zapada - src/kernel/sched/kstack.c
 *
 * Kernel stack pool for Phase 2C.
 *
 * Each kernel thread needs its own stack for context-switch save/restore.
 * This module provides a static pool of KSTACK_COUNT stacks, each
 * KSTACK_SIZE bytes.
 *
 * Architecture-specific initial frame layout:
 *
 * x86_64 (System V AMD64 ABI, callee-saved: rbp, rbx, r12-r15):
 *   ctx.sp = kstack_top - 56
 *   [ctx.sp+0]  = r15 = 0
 *   [ctx.sp+8]  = r14 = 0
 *   [ctx.sp+16] = r13 = 0
 *   [ctx.sp+24] = r12 = 0
 *   [ctx.sp+32] = rbx = (uint64_t)entry   <- passes entry_fn to trampoline
 *   [ctx.sp+40] = rbp = 0
 *   [ctx.sp+48] = thread_trampoline_x86   <- `ret` in context_switch jumps here
 *
 * AArch64 (AAPCS64, callee-saved: x19-x28, x29 FP, x30 LR):
 *   ctx.sp = kstack_top - 96
 *   [ctx.sp+0]  = x19 = (uint64_t)entry   <- passes entry_fn to trampoline
 *   [ctx.sp+8]  = x20 = 0
 *   ...
 *   [ctx.sp+80] = x29 = 0  (frame pointer)
 *   [ctx.sp+88] = x30 = thread_trampoline_aa64  <- `ret` jumps here
 *
 * Trampolines (defined in context_switch.asm / context_switch.S):
 *   thread_trampoline_x86 / thread_trampoline_aa64
 *     Each calls the entry function (from rbx / x19) and loops forever
 *     if entry returns (no exit path for Phase 2C kernel threads).
 */

#include <kernel/sched/kstack.h>
#include <kernel/console.h>
#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Static kernel stack pool                                                */
/* ---------------------------------------------------------------------- */

/*
 * The stack pool is 16-byte aligned so both x86_64 and AArch64 receive
 * a properly aligned initial SP. KSTACK_SIZE is already a multiple of 16.
 */
__attribute__((aligned(16)))
static uint8_t s_kstacks[KSTACK_COUNT][KSTACK_SIZE];
static bool    s_kstack_used[KSTACK_COUNT];

/* ---------------------------------------------------------------------- */
/* Architecture-specific trampoline declarations                           */
/* ---------------------------------------------------------------------- */

#if defined(__x86_64__)
extern void thread_trampoline_x86(void);
#elif defined(__aarch64__)
extern void thread_trampoline_aa64(void);
#endif

/* ---------------------------------------------------------------------- */
/* kstack_init                                                             */
/* ---------------------------------------------------------------------- */

void kstack_init(void)
{
    uint32_t i;

    for (i = 0; i < KSTACK_COUNT; i++) {
        s_kstack_used[i] = false;
    }

    console_write("KStack pool     : ");
    console_write_dec((uint64_t)KSTACK_COUNT);
    console_write(" x ");
    console_write_dec((uint64_t)KSTACK_SIZE);
    console_write(" bytes initialized\n");
}

/* ---------------------------------------------------------------------- */
/* kstack_alloc                                                            */
/* ---------------------------------------------------------------------- */

void *kstack_alloc(void)
{
    uint32_t i;

    for (i = 0; i < KSTACK_COUNT; i++) {
        if (!s_kstack_used[i]) {
            s_kstack_used[i] = true;
            /* Return pointer just past the end of the stack (the "top") */
            return (void *)(&s_kstacks[i][KSTACK_SIZE]);
        }
    }

    console_write("KStack pool     : EXHAUSTED (no free kernel stacks)\n");
    return NULL;
}

/* ---------------------------------------------------------------------- */
/* kstack_free                                                             */
/* ---------------------------------------------------------------------- */

void kstack_free(void *top)
{
    uint32_t i;
    uint8_t *base;

    if (top == NULL) {
        return;
    }

    for (i = 0; i < KSTACK_COUNT; i++) {
        base = &s_kstacks[i][KSTACK_SIZE];
        if ((void *)base == top) {
            s_kstack_used[i] = false;
            return;
        }
    }

    console_write("KStack pool     : kstack_free: unrecognized stack top\n");
}

/* ---------------------------------------------------------------------- */
/* kstack_init_context                                                     */
/* ---------------------------------------------------------------------- */

bool kstack_init_context(thread_t *t, void *kstack_top,
                          void (*entry)(void))
{
    uint64_t *sp;

    if (t == NULL || kstack_top == NULL) {
        return false;
    }

    /* Store kstack_base (bottom of stack = top - size) for later free */
    t->kstack_base = (uint8_t *)kstack_top - KSTACK_SIZE;

#if defined(__x86_64__)
    /*
     * x86_64 initial frame layout (7 quadwords = 56 bytes):
     *   sp[0] = r15 = 0
     *   sp[1] = r14 = 0
     *   sp[2] = r13 = 0
     *   sp[3] = r12 = 0
     *   sp[4] = rbx = entry   <- trampoline reads this as the actual entry function
     *   sp[5] = rbp = 0
     *   sp[6] = &thread_trampoline_x86   <- return address used by RET
     *
     * sched_x86_context_switch pops r15, r14, r13, r12, rbx, rbp then RET.
     * RET jumps to sp[6] = thread_trampoline_x86.
     * thread_trampoline_x86 calls rbx (= entry).
     */
    sp    = (uint64_t *)kstack_top - 7;
    sp[0] = 0u;                                          /* r15 */
    sp[1] = 0u;                                          /* r14 */
    sp[2] = 0u;                                          /* r13 */
    sp[3] = 0u;                                          /* r12 */
    sp[4] = (uint64_t)(uintptr_t)entry;                  /* rbx = entry_fn */
    sp[5] = 0u;                                          /* rbp */
    sp[6] = (uint64_t)(uintptr_t)thread_trampoline_x86; /* return addr */

    t->ctx.sp = (uint64_t)(uintptr_t)sp;
    t->ctx.ip = (uint64_t)(uintptr_t)entry;

#elif defined(__aarch64__)
    /*
     * AArch64 initial frame layout (12 quadwords = 96 bytes).
     *
     * IMPORTANT: this must match the exact memory layout produced by
     * sched_aa64_context_switch() after saving a running thread.
     * That save path pushes these pairs in order:
     *   stp x19,x20
     *   stp x21,x22
     *   stp x23,x24
     *   stp x25,x26
     *   stp x27,x28
     *   stp x29,x30
     *
     * Because the stack grows downward, the LOWEST address (new SP) contains
     * the LAST pushed pair: x29/x30. The restore path then pops in this order:
     *   ldp x29,x30
     *   ldp x27,x28
     *   ldp x25,x26
     *   ldp x23,x24
     *   ldp x21,x22
     *   ldp x19,x20
     *
     * Therefore the synthetic first-dispatch frame must be laid out as:
     *   sp[0]  = x29 = 0
     *   sp[1]  = x30 = &thread_trampoline_aa64  <- RET target (lr)
     *   sp[2]  = x27 = 0
     *   sp[3]  = x28 = 0
     *   sp[4]  = x25 = 0
     *   sp[5]  = x26 = 0
     *   sp[6]  = x23 = 0
     *   sp[7]  = x24 = 0
     *   sp[8]  = x21 = 0
     *   sp[9]  = x22 = 0
     *   sp[10] = x19 = entry  <- trampoline reads this as the actual entry function
     *   sp[11] = x20 = 0
     */
    sp     = (uint64_t *)kstack_top - 12;
    sp[0]  = 0u;                                            /* x29 (fp) */
    sp[1]  = (uint64_t)(uintptr_t)thread_trampoline_aa64;  /* x30 (lr) */
    sp[2]  = 0u;                                            /* x27 */
    sp[3]  = 0u;                                            /* x28 */
    sp[4]  = 0u;                                            /* x25 */
    sp[5]  = 0u;                                            /* x26 */
    sp[6]  = 0u;                                            /* x23 */
    sp[7]  = 0u;                                            /* x24 */
    sp[8]  = 0u;                                            /* x21 */
    sp[9]  = 0u;                                            /* x22 */
    sp[10] = (uint64_t)(uintptr_t)entry;                   /* x19 = entry_fn */
    sp[11] = 0u;                                            /* x20 */

    t->ctx.sp = (uint64_t)(uintptr_t)sp;
    t->ctx.ip = (uint64_t)(uintptr_t)entry;

#else
    /* Unsupported architecture: context setup is a no-op */
    t->ctx.sp = 0u;
    t->ctx.ip = 0u;
    (void)entry;
#endif

    t->ctx.flags = 0u;
    return true;
}

