/*
 * Zapada - src/kernel/arch/aarch64/gentimer.c
 *
 * ARM Generic Timer (EL1 Physical Timer) initialization for Phase 2C.
 *
 * System registers used (all accessible from EL1):
 *   CNTFRQ_EL0   - counter frequency in Hz (read-only, set by firmware)
 *   CNTP_CTL_EL0 - EL1 physical timer control:
 *                    bit 0: ENABLE (1 = timer enabled)
 *                    bit 1: IMASK  (1 = IRQ masked at timer, 0 = IRQ active)
 *                    bit 2: ISTATUS (read-only, 1 = condition met / timer fired)
 *   CNTP_TVAL_EL0 - EL1 physical timer value (countdown; decrements at
 *                   CNTFRQ Hz; fires when reaching 0 or below)
 *
 * Note: mstrict-align is active; all register access uses MRS/MSR
 * instructions, not memory-mapped I/O. No alignment issues arise here.
 */

#include <kernel/arch/aarch64/gentimer.h>
#include <kernel/sched/timer.h>
#include <kernel/console.h>
#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* Module state                                                            */
/* ---------------------------------------------------------------------- */

static uint32_t s_hz;
static uint64_t s_reload_value;

/* ---------------------------------------------------------------------- */
/* gentimer_init                                                           */
/* ---------------------------------------------------------------------- */

void gentimer_init(uint32_t hz)
{
    uint64_t cntfrq;
    uint64_t tval;

    s_hz = (hz == 0u) ? 1u : hz;

    /* Read the counter frequency provided by QEMU/firmware */
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(cntfrq));

    /* Compute ticks per period */
    tval = cntfrq / (uint64_t)s_hz;
    if (tval < 1u) {
        tval = 1u;
    }
    s_reload_value = tval;

    /*
     * Program the countdown timer:
     *   1. Disable the timer while configuring (CTL = 0).
     *   2. Load the initial countdown value.
     *   3. Enable timer with IRQ unmasked (CTL = ENABLE = 1).
     */
    __asm__ volatile ("msr cntp_ctl_el0, %0" : : "r"((uint64_t)0u));
    __asm__ volatile ("msr cntp_tval_el0, %0" : : "r"(tval));
    __asm__ volatile ("msr cntp_ctl_el0, %0" : : "r"((uint64_t)1u));
    __asm__ volatile ("isb");

    console_write("GenTimer        : EL1 physical timer hz=");
    console_write_dec((uint64_t)s_hz);
    console_write(" cntfrq=");
    console_write_dec(cntfrq);
    console_write(" tval=");
    console_write_dec(tval);
    console_write(", IRQ pending-ready\n");
}

/* ---------------------------------------------------------------------- */
/* aarch64_gentimer_irq_handler                                           */
/* ---------------------------------------------------------------------- */

/*
 * Called from exception_vectors.S on EL1h IRQ (vector 5).
 *
 * Acknowledge / disable the timer briefly (prevent re-entry), reload
 * the countdown for the next tick, re-enable, then call timer_tick_handler().
 *
 * The timer control register write sequence:
 *   1. Mask the timer interrupt (CTL |= IMASK) to clear the pending condition.
 *   2. Reload CNTP_TVAL_EL0 with s_reload_value.
 *   3. Unmask the interrupt (CTL = ENABLE only, IMASK cleared).
 */
void aarch64_gentimer_irq_handler(void)
{
    /* Step 1: acknowledge by masking (bit 1 = IMASK set, bit 0 = ENABLE) */
    __asm__ volatile ("msr cntp_ctl_el0, %0" : : "r"((uint64_t)3u));

    /* Step 2: reload countdown */
    __asm__ volatile ("msr cntp_tval_el0, %0" : : "r"(s_reload_value));

    /* Step 3: re-enable with IMASK cleared (CTL = ENABLE = 1, IMASK = 0) */
    __asm__ volatile ("msr cntp_ctl_el0, %0" : : "r"((uint64_t)1u));
    __asm__ volatile ("isb");

    /* Dispatch to the arch-neutral timer tick handler */
    timer_tick_handler();
}

