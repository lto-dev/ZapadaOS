/*
 * Zapada - src/kernel/arch/x86_64/pit.h
 *
 * Intel 8253/8254 Programmable Interval Timer (PIT) interface.
 *
 * Phase 2C: PIT channel 0 is programmed to fire IRQ0 at the requested
 * frequency. The ISR at IDT vector 0x20 calls timer_tick_handler(), which
 * dispatches to the registered scheduler tick callback.
 *
 * Design:
 *   - Only channel 0 is used (drives IRQ0).
 *   - Mode 2 (rate generator): periodic, self-reloading.
 *   - Divisor = PIT_BASE_HZ / hz. Clamped to [1, 65535].
 *   - IRQ0 is unmasked in the PIC after the PIT is programmed.
 */

#ifndef ZAPADA_ARCH_X86_64_PIT_H
#define ZAPADA_ARCH_X86_64_PIT_H

#include <kernel/types.h>

/*
 * PIT oscillator frequency (Hz). The counter is loaded with
 * PIT_BASE_HZ / desired_hz to achieve the requested tick rate.
 */
#define PIT_BASE_HZ  1193182u

/*
 * pit_init - program PIT channel 0 and unmask IRQ0 in the PIC.
 *
 * hz - desired timer frequency in Hz (e.g. 100 for 10 ms ticks).
 *
 * Must be called after both pic_init() and idt_init() (which installs
 * irq0_stub at IDT vector 0x20).
 *
 * Prints diagnostic output on the console.
 */
void pit_init(uint32_t hz);

/*
 * irq0_timer_c_handler - C-level handler called from irq0_stub (isr.asm).
 *
 * Sends PIC EOI for IRQ0, then calls timer_tick_handler() to dispatch
 * the registered scheduler tick callback.
 *
 * Called with interrupts disabled (CPU cleared IF on entry to the ISR).
 * Does not re-enable interrupts; IRETQ in irq0_stub restores RFLAGS
 * (including IF) from the pre-interrupt RFLAGS saved on the stack.
 */
void irq0_timer_c_handler(void);

#endif /* ZAPADA_ARCH_X86_64_PIT_H */


