/*
 * Zapada - src/kernel/arch/aarch64/gentimer.h
 *
 * ARM Generic Timer (EL1 Physical Timer) interface for Phase 2C.
 *
 * Design:
 *   - Uses the EL1 Physical Timer (CNTP_*_EL0 registers).
 *   - Fires at the requested Hz via a periodic countdown reload.
 *   - The interrupt is delivered as EL1h IRQ (vector 5 in the exception
 *     vector table). exception_vectors.S dispatches it to
 *     aarch64_gentimer_irq_handler().
 *   - After the IRQ fires, the timer is reloaded and enabled for the
 *     next tick.
 *
 * IRQ enable:
 *   - DAIFCLR #2 unmasks IRQ at EL1. This is called from gentimer_init()
 *     AFTER the timer is programmed, so no spurious IRQ fires before the
 *     handler is ready.
 *
 * QEMU raspi3b note:
 *   - CNTFRQ_EL0 reports the counter frequency. QEMU sets this to
 *     62500000 Hz for the raspi3b machine.
 *   - CNTP_TVAL_EL0 = CNTFRQ / hz gives a countdown to the next tick.
 */

#ifndef ZAPADA_ARCH_AARCH64_GENTIMER_H
#define ZAPADA_ARCH_AARCH64_GENTIMER_H

#include <kernel/types.h>

/*
 * gentimer_init - program the EL1 physical timer and enable IRQs.
 *
 * hz - desired tick rate in Hz (e.g. 100).
 *
 * Programs CNTP_TVAL_EL0 and CNTP_CTL_EL0 for the first countdown, then
 * unmasks IRQ at EL1 via DAIFCLR. Must be called after exception vectors
 * are installed (exception_init) so the IRQ handler is in place.
 *
 * Prints diagnostic output including the detected CNTFRQ value.
 */
void gentimer_init(uint32_t hz);

/*
 * aarch64_gentimer_irq_handler - C handler called from exception_vectors.S
 * on EL1h IRQ (vector 5).
 *
 * Reloads the timer countdown for the next tick, then calls
 * timer_tick_handler() to dispatch to the registered scheduler callback.
 *
 * Called with IRQs masked (DAIF.I = 1 on exception entry).
 * ERET in the vector entry restores SPSR_EL1 including DAIF.
 */
void aarch64_gentimer_irq_handler(void);

#endif /* ZAPADA_ARCH_AARCH64_GENTIMER_H */


