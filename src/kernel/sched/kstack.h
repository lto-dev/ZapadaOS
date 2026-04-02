/*
 * Zapada - src/kernel/sched/kstack.h
 *
 * Kernel stack pool for Phase 2C.
 *
 * Design:
 *   - Statically allocated pool of KSTACK_COUNT kernel stacks, each
 *     KSTACK_SIZE bytes.
 *   - Allocation is O(n) linear scan over the used-flags array.
 *     Acceptable for Phase 2C with KSTACK_COUNT = 32.
 *   - No heap dependency. All storage is in BSS.
 *   - AArch64 alignment: KSTACK_SIZE is a multiple of 16 and the pool
 *     is 16-byte aligned via __attribute__((aligned(16))).
 *
 * Usage:
 *   1. kstack_init()         - called once at Phase 2C init.
 *   2. void *top = kstack_alloc() - get the top-of-stack for a new thread.
 *      (top is the address just past the last byte of the stack: use for
 *       initial stack pointer setup and for kstack_init_context()).
 *   3. kstack_free(top)      - return the stack when the thread exits.
 *
 * kstack_init_context:
 *   Prepares the initial saved-register frame on a new thread's kernel
 *   stack so that the first context switch to this thread dispatches
 *   to the thread's entry function. After this call, thread->ctx.sp
 *   holds the saved RSP/SP and thread->ctx.ip holds the entry function.
 */

#ifndef ZAPADA_SCHED_KSTACK_H
#define ZAPADA_SCHED_KSTACK_H

#include <kernel/process/process.h>
#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Constants                                                               */
/* ---------------------------------------------------------------------- */

/*
 * KSTACK_COUNT - number of kernel stacks in the static pool.
 * Matches SCHED_MAX_THREADS so every schedulable thread can have its own
 * kernel stack simultaneously.
 */
#define KSTACK_COUNT  32u

/*
 * KSTACK_SIZE - bytes per kernel stack.
 * Must be a multiple of 16 for AArch64 SP alignment.
 *
 * The CLR interpreter currently compiles into a very large dispatch function;
 * recent gap-closure work increased its x86_64 stack frame well beyond 4 KiB.
 * Keep the kernel thread stack comfortably above that frame size so managed
 * execution does not corrupt adjacent state or fault while handling ldstr /
 * InternalCall-heavy conformance paths.
 */
#define KSTACK_SIZE   65536u

/* ---------------------------------------------------------------------- */
/* API                                                                     */
/* ---------------------------------------------------------------------- */

/*
 * kstack_init - initialize the kernel stack pool.
 * Must be called once before any kstack_alloc() call.
 */
void kstack_init(void);

/*
 * kstack_alloc - allocate one kernel stack from the pool.
 *
 * Returns a pointer to the TOP of the allocated stack (the address one
 * byte past the end of the stack region, suitable as the initial stack
 * pointer). Returns NULL if the pool is exhausted.
 */
void *kstack_alloc(void);

/*
 * kstack_free - return a previously allocated kernel stack to the pool.
 *
 * top - the value returned by kstack_alloc() for this stack.
 * Does nothing if top is NULL or not a recognized stack-top address.
 */
void kstack_free(void *top);

/*
 * kstack_init_context - set up the initial context on a new thread's stack.
 *
 * Pushes a fake saved-register frame (callee-saved regs = 0) and the
 * entry function address as the "return address" that the context switch
 * assembly will jump to on first dispatch.
 *
 * After this call:
 *   t->ctx.sp  = stack pointer pointing to the prepared frame
 *   t->ctx.ip  = entry (stored for diagnostic/debug purposes)
 *   t->kstack_base = base of the allocated stack (bottom address)
 *
 * Parameters:
 *   t         - thread whose context to initialize (must not be NULL)
 *   kstack_top - top of allocated kernel stack (from kstack_alloc())
 *   entry     - function to call when this thread first runs
 *
 * Returns true on success, false if t or kstack_top is NULL.
 */
bool kstack_init_context(thread_t *t, void *kstack_top,
                          void (*entry)(void));

#endif /* ZAPADA_SCHED_KSTACK_H */


