/*
 * Zapada - src/kernel/phase2c.h
 *
 * Phase 2C orchestration and self-test header.
 *
 * Phase 2C deliverables:
 *   - Hardware timer wiring: x86_64 PIT + AArch64 ARM generic timer
 *   - Real CPU context switch: callee-saved register save/restore on both
 *     architectures via sched_x86_context_switch / sched_aa64_context_switch
 *   - Kernel stack pool: static pool of KSTACK_COUNT stacks for kernel threads
 *   - Blocking IPC: ipc_send / ipc_recv block calling thread when full/empty,
 *     integrated with sched_block / sched_unblock / sched_context_switch
 *   - User-mode groundwork: GDT ring-3 entries, user_isr_frame_t in idt.h
 *   - Managed scheduler policy syscalls: SCHED_SET_PRIORITY, SCHED_SLEEP,
 *     SCHED_GET_PRIORITY via the syscall dispatch table
 *
 * Self-test coverage:
 *   - 10 focused tests covering timer, kstack, context switch, IPC, syscall,
 *     and architecture-specific structures.
 */

#ifndef ZAPADA_PHASE2C_H
#define ZAPADA_PHASE2C_H

/* ---------------------------------------------------------------------- */
/* Phase 2C initialization                                                 */
/* ---------------------------------------------------------------------- */

/*
 * phase2c_init - initialize all Phase 2C subsystems and run self-tests.
 *
 * Call sequence:
 *   1. Initialize kernel stack pool (kstack_init).
 *   2. Wire the architecture-specific hardware timer into timer_tick_handler.
 *   3. Run the Phase 2C self-test suite (10 tests).
 *   4. Print pass/fail summary.
 *
 * Must be called after phase2b_init().
 */
void phase2c_init(void);

#endif /* ZAPADA_PHASE2C_H */


