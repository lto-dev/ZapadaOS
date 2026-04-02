/*
 * Zapada - src/kernel/sched/timer.h
 *
 * Timer integration stubs for Phase 2B preemption hook points.
 *
 * Design:
 *   - Architecture-neutral interface. The arch-specific implementations
 *     (x86_64 PIT stub, AArch64 generic timer stub) register a callback
 *     via timer_set_tick_callback().
 *   - Phase 2B: stubs only. No actual timer interrupt delivery. The tick
 *     function is called manually from the kernel boot sequence to verify
 *     the scheduling tick path.
 *   - Phase 2C will wire real interrupt delivery to timer_tick_handler().
 */

#ifndef ZAPADA_TIMER_H
#define ZAPADA_TIMER_H

#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Timer tick callback type                                                */
/* ---------------------------------------------------------------------- */

/*
 * timer_tick_fn_t - function called on each timer tick.
 *
 * Implementations should call sched_tick(current_thread) and then call
 * sched_next() if a context switch is needed.
 */
typedef void (*timer_tick_fn_t)(void);

/* ---------------------------------------------------------------------- */
/* Timer API                                                               */
/* ---------------------------------------------------------------------- */

/*
 * timer_init - initialize the timer subsystem.
 *
 * Phase 2B: no real hardware timer is programmed. Prints diagnostic output
 * and records the tick frequency for future use.
 *
 * hz - intended tick frequency in Hz (stored, not yet used for HW).
 */
void timer_init(uint32_t hz);

/*
 * timer_set_tick_callback - register the function called on each timer tick.
 *
 * Only one callback is supported. Subsequent calls replace the previous one.
 * Pass NULL to clear the callback.
 */
void timer_set_tick_callback(timer_tick_fn_t fn);

/*
 * timer_tick_handler - entry point for the timer interrupt.
 *
 * Called from the architecture-specific interrupt handler (ISR or exception
 * handler). Invokes the registered tick callback if one is set.
 *
 * Phase 2B: also callable directly (simulated tick) from boot sequence tests.
 */
void timer_tick_handler(void);

/*
 * timer_get_tick_count - return the total number of ticks since timer_init.
 */
uint64_t timer_get_tick_count(void);

/*
 * timer_get_hz - return the configured tick frequency.
 */
uint32_t timer_get_hz(void);

#endif /* ZAPADA_TIMER_H */


