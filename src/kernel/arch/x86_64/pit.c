/*
 * Zapada - src/kernel/arch/x86_64/pit.c
 *
 * Intel 8253/8254 PIT channel 0 initialization for Phase 2C.
 *
 * PIT I/O ports:
 *   Channel 0 data: 0x40   (read/write 16-bit divisor, low byte first)
 *   Channel 1 data: 0x41   (not used)
 *   Channel 2 data: 0x42   (not used)
 *   Command/mode:   0x43
 *
 * Mode register byte for channel 0, mode 2:
 *   Bits [7:6] = 00  (channel 0)
 *   Bits [5:4] = 11  (access: low byte then high byte)
 *   Bits [3:1] = 010 (mode 2: rate generator, periodic)
 *   Bit  [0]   = 0   (binary count)
 *   => 0x34
 */

#include <kernel/arch/x86_64/pit.h>
#include <kernel/arch/x86_64/pic.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/sched/timer.h>
#include <kernel/console.h>
#include <kernel/types.h>

/* PIT I/O port addresses */
#define PIT_CH0_DATA   0x40u
#define PIT_CMD        0x43u

/* Mode byte: channel 0, lo/hi access, mode 2 (rate generator), binary */
#define PIT_MODE2_CH0  0x34u

/* ---------------------------------------------------------------------- */
/* pit_init                                                                */
/* ---------------------------------------------------------------------- */

void pit_init(uint32_t hz)
{
    uint32_t divisor;
    uint8_t  lo;
    uint8_t  hi;

    /*
     * Compute divisor. Clamp to valid [1, 65535] range.
     * A divisor of 0 is treated as 65536 by the hardware (maximum period);
     * we avoid it by clamping to 1 (maximum rate) at the low end.
     */
    if (hz == 0u) {
        hz = 1u;
    }

    divisor = PIT_BASE_HZ / hz;
    if (divisor < 1u) {
        divisor = 1u;
    }
    if (divisor > 65535u) {
        divisor = 65535u;
    }

    lo = (uint8_t)(divisor & 0xFFu);
    hi = (uint8_t)((divisor >> 8) & 0xFFu);

    /* Program channel 0 in mode 2 (rate generator) */
    outb(PIT_CMD, PIT_MODE2_CH0);
    outb(PIT_CH0_DATA, lo);
    outb(PIT_CH0_DATA, hi);

    /* Unmask IRQ0 so the PIC forwards timer interrupts to the CPU */
    pic_unmask_irq(PIC_IRQ_TIMER);

    console_write("PIT             : channel 0 programmed at hz=");
    console_write_dec((uint64_t)hz);
    console_write(" divisor=");
    console_write_dec((uint64_t)divisor);
    console_write(", IRQ0 unmasked\n");
}

/* ---------------------------------------------------------------------- */
/* irq0_timer_c_handler                                                    */
/* ---------------------------------------------------------------------- */

/*
 * Called from irq0_stub (isr.asm) on every PIT channel 0 interrupt.
 * Sends PIC EOI first (to allow new IRQs to be accepted by the PIC),
 * then dispatches to the timer tick subsystem.
 *
 * Interrupts are disabled during this call (CPU cleared IF on ISR entry).
 * The IRETQ in irq0_stub restores IF from the saved RFLAGS.
 */
void irq0_timer_c_handler(void)
{
    pic_send_eoi(PIC_IRQ_TIMER);
    timer_tick_handler();
}

