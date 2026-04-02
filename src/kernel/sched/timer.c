/*
 * Zapada - src/kernel/sched/timer.c
 *
 * Architecture-neutral timer layer for Phase 2B/2C.
 *
 * Phase 2C: timer_init() now programs the real hardware timer:
 *   - x86_64: calls pit_init(hz) to configure the 8254 PIT at the given
 *     frequency. The PIT fires IRQ0 -> irq0_stub -> irq0_timer_c_handler ->
 *     timer_tick_handler().
 *   - AArch64: calls gentimer_init(hz) to configure the ARM EL1 Physical
 *     Timer. Countdown fires as an EL1h IRQ -> el1h_irq_handler_stub ->
 *     aarch64_gentimer_irq_handler -> timer_tick_handler().
 *
 * timer_tick_handler() increments s_tick_count and calls the registered
 * tick callback (if any).
 */

#include <kernel/sched/timer.h>
#include <kernel/console.h>
#include <kernel/types.h>

#if defined(__x86_64__)
#include <kernel/arch/x86_64/pic.h>
#include <kernel/arch/x86_64/pit.h>
#elif defined(__aarch64__)
#include <kernel/arch/aarch64/gentimer.h>
#endif

/* ---------------------------------------------------------------------- */
/* Module state                                                            */
/* ---------------------------------------------------------------------- */

static uint64_t       s_tick_count;
static uint32_t       s_hz;
static timer_tick_fn_t s_tick_fn;

/* ---------------------------------------------------------------------- */
/* timer_init                                                              */
/* ---------------------------------------------------------------------- */

void timer_init(uint32_t hz)
{
    s_tick_count = 0;
    s_hz         = hz;
    s_tick_fn    = NULL;

    console_write("Timer           : initializing hz=");
    console_write_dec((uint64_t)hz);
    console_write("\n");

#if defined(__x86_64__)
    /*
     * x86_64: initialize the 8259A PIC (remap IRQs to 0x20-0x2F, mask all),
     * then program the PIT channel 0 in rate-generator mode.
     * pit_init() also unmasks PIC IRQ0 so ticks begin immediately after
     * the first STI.
     */
    pic_init();
    pit_init(hz);
    console_write("Timer           : x86_64 PIC + PIT programmed\n");
#elif defined(__aarch64__)
    /*
     * AArch64: program the ARM EL1 Physical Timer (CNTP) at the given
     * frequency. gentimer_init() enables the timer and unmasks IRQs.
     */
    gentimer_init(hz);
    console_write("Timer           : AArch64 generic timer programmed\n");
#else
    console_write("Timer           : no HW timer for this architecture\n");
#endif
}

/* ---------------------------------------------------------------------- */
/* timer_set_tick_callback                                                 */
/* ---------------------------------------------------------------------- */

void timer_set_tick_callback(timer_tick_fn_t fn)
{
    s_tick_fn = fn;
}

/* ---------------------------------------------------------------------- */
/* timer_tick_handler                                                      */
/* ---------------------------------------------------------------------- */

void timer_tick_handler(void)
{
    s_tick_count++;

    if (s_tick_fn != NULL) {
        s_tick_fn();
    }
}

/* ---------------------------------------------------------------------- */
/* timer_get_tick_count                                                    */
/* ---------------------------------------------------------------------- */

uint64_t timer_get_tick_count(void)
{
    return s_tick_count;
}

/* ---------------------------------------------------------------------- */
/* timer_get_hz                                                            */
/* ---------------------------------------------------------------------- */

uint32_t timer_get_hz(void)
{
    return s_hz;
}

