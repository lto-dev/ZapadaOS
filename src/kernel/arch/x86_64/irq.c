/*
 * Zapada - src/kernel/arch/x86_64/irq.c
 *
 * Generic x86_64 PIC IRQ dispatch for managed driver event delivery.
 */

#include <kernel/arch/x86_64/pic.h>
#include <kernel/irq/irq_router.h>
#include <kernel/types.h>

void x86_irq_c_handler(uint32_t irq)
{
    pic_send_eoi(irq);
    irq_router_publish(irq);
}
