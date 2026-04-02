/*
 * Zapada - src/kernel/sched/sched.h
 *
 * Scheduler foundation for Phase 2B/2C.
 *
 * Design principles:
 *   - Deterministic run queue: round-robin across READY threads ordered by
 *     priority (lower value = higher priority). Within a priority band,
 *     FIFO insertion order is preserved.
 *   - Bounded structure: static array of per-priority queues (SCHED_PRIO_MAX
 *     bands). No dynamic allocation in the scheduler core.
 *   - Preemption hook points: sched_tick() is called from the timer interrupt
 *     handler (Phase 2C: real hardware timer delivery on both architectures).
 *   - Managed-first: thread_t.managed_ctx links to CLR execution contexts;
 *     the scheduler does not interpret managed state.
 *
 * Phase 2C additions:
 *   - sched_get_current() / sched_set_current(): global current-thread pointer.
 *   - sched_context_switch(): performs a real CPU context switch by invoking
 *     the arch-specific assembly (context_switch.asm / context_switch.S).
 */

#ifndef ZAPADA_SCHED_H
#define ZAPADA_SCHED_H

#include <kernel/process/process.h>
#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Scheduler constants                                                     */
/* ---------------------------------------------------------------------- */

/*
 * SCHED_PRIO_MAX - number of priority bands (0 = highest, SCHED_PRIO_MAX-1
 * = lowest). Phase 2B uses 8 bands, sufficient for kernel + managed threads.
 */
#define SCHED_PRIO_MAX          8u

/*
 * SCHED_DEFAULT_QUANTUM - default ticks a thread may run before preemption.
 * A tick corresponds to one sched_tick() call. Timer period is
 * architecture-specific and configured separately.
 */
#define SCHED_DEFAULT_QUANTUM   10u

/*
 * SCHED_MAX_THREADS - maximum total threads across all queues. Static
 * allocation in Phase 2B; no heap dependency in the scheduler.
 */
#define SCHED_MAX_THREADS       32u

/* ---------------------------------------------------------------------- */
/* Scheduler state                                                         */
/* ---------------------------------------------------------------------- */

/*
 * sched_state_t - internal scheduler state flags.
 */
typedef enum {
    SCHED_IDLE       = 0,   /* No runnable threads */
    SCHED_RUNNING    = 1,   /* At least one READY thread exists */
    SCHED_PREEMPT    = 2,   /* Preemption requested (tick expired) */
} sched_state_t;

/* ---------------------------------------------------------------------- */
/* Scheduler API                                                           */
/* ---------------------------------------------------------------------- */

/*
 * sched_init - initialize the scheduler run queues and internal state.
 *
 * Must be called once during kernel initialization, after process subsystem
 * is ready. Clears all queue heads and resets scheduler state.
 */
void sched_init(void);

/*
 * sched_enqueue - add a thread to the appropriate priority run queue.
 *
 * The thread must be in READY state. Inserts at the tail of its priority
 * band (FIFO within band). Returns true on success, false if:
 *   - t is NULL
 *   - t->state != PROCESS_STATE_READY
 *   - t->priority >= SCHED_PRIO_MAX
 *   - run queue is full (>= SCHED_MAX_THREADS total)
 */
bool sched_enqueue(thread_t *t);

/*
 * sched_dequeue - remove and return the highest-priority READY thread.
 *
 * Scans priority bands from 0 (highest) to SCHED_PRIO_MAX-1. Removes the
 * head of the first non-empty band. Returns NULL if all queues are empty.
 *
 * The returned thread is NOT automatically set to RUNNING; the caller must
 * transition state after context switching.
 */
thread_t *sched_dequeue(void);

/*
 * sched_yield - move the current thread back to READY and enqueue it.
 *
 * Resets ticks_remaining to SCHED_DEFAULT_QUANTUM. Returns true on success,
 * false if current is NULL or not in RUNNING state.
 */
bool sched_yield(thread_t *current);

/*
 * sched_tick - called from the timer interrupt stub to advance the scheduler.
 *
 * Decrements current->ticks_remaining. When it reaches zero, sets the
 * SCHED_PREEMPT flag. Does nothing if current is NULL.
 *
 * Phase 2B: no actual preemption occurs; flag is inspected by sched_next().
 */
void sched_tick(thread_t *current);

/*
 * sched_next - select the next thread to run.
 *
 * If SCHED_PREEMPT is set and current is still RUNNING, re-queues current
 * (as if it yielded) before selecting the next thread.
 *
 * Returns:
 *   - The next thread to run (highest priority READY thread).
 *   - current, if no other READY thread exists and current is still RUNNING.
 *   - NULL if no runnable threads exist (scheduler enters IDLE state).
 */
thread_t *sched_next(thread_t *current);

/*
 * sched_block - remove the current thread from the run queue and mark it
 * BLOCKED. Returns true on success.
 *
 * The thread will not be scheduled again until sched_unblock() is called.
 */
bool sched_block(thread_t *t);

/*
 * sched_unblock - transition a BLOCKED thread back to READY and enqueue it.
 *
 * Returns true on success, false if t is NULL or t->state != BLOCKED.
 */
bool sched_unblock(thread_t *t);

/*
 * sched_get_state - return the current scheduler state flag.
 */
sched_state_t sched_get_state(void);

/*
 * sched_get_ready_count - return the total number of READY threads across
 * all priority queues.
 */
uint32_t sched_get_ready_count(void);

/*
 * sched_print_queues - print a diagnostic dump of all non-empty run queues
 * to the console.
 */
void sched_print_queues(void);

/* ---------------------------------------------------------------------- */
/* Phase 2C: current thread tracking and real context switch               */
/* ---------------------------------------------------------------------- */

/*
 * sched_get_current - return a pointer to the currently running thread.
 *
 * Returns NULL when the scheduler is idle (no thread is running).
 */
thread_t *sched_get_current(void);

/*
 * sched_set_current - record the thread that is now running on the CPU.
 *
 * Called by sched_context_switch() after selecting the next thread.
 * Callers outside the scheduler core should not normally call this directly.
 */
void sched_set_current(thread_t *t);

/*
 * sched_context_switch - perform a real CPU context switch from prev to next.
 *
 * Saves callee-saved registers on prev's kernel stack (via
 * sched_x86_context_switch / sched_aa64_context_switch), updates
 * prev->ctx.sp and next->ctx.sp, then restores callee-saved registers from
 * next's kernel stack and returns into next's execution context.
 *
 * Transitions:
 *   - prev state is not modified here; caller must set prev->state before
 *     calling (e.g., READY for preemption, BLOCKED for IPC wait).
 *   - next->state is set to PROCESS_STATE_RUNNING before the switch.
 *
 * Safety:
 *   - prev and next must not be NULL.
 *   - next->ctx.sp must be valid (initialized via kstack_init_context or a
 *     previous context switch save).
 *   - Must not be called with interrupts enabled on an uninitialized stack.
 */
void sched_context_switch(thread_t *prev, thread_t *next);

#endif /* ZAPADA_SCHED_H */


