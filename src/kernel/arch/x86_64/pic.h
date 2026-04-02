/*
 * Zapada - src/kernel/arch/x86_64/pic.h
 *
 * Intel 8259A Programmable Interrupt Controller (PIC) interface.
 *
 * Phase 2C: The 8259A dual-PIC chain is initialized and remapped so that
 * IRQ0-IRQ7 map to IDT vectors 0x20-0x27 and IRQ8-IRQ15 map to 0x28-0x2F.
 * This avoids conflicts with the CPU exception vectors (0x00-0x1F).
 *
 * After remapping, all IRQ lines except IRQ0 (timer) are masked. Phase 2C
 * unmasks only IRQ0. Additional IRQ lines are enabled as drivers are added
 * in later phases.
 */

#ifndef ZAPADA_ARCH_X86_64_PIC_H
#define ZAPADA_ARCH_X86_64_PIC_H

#include <kernel/types.h>

/* IDT vector base for IRQ0-IRQ7 (master PIC) */
#define PIC_MASTER_OFFSET   0x20u

/* IDT vector base for IRQ8-IRQ15 (slave PIC) */
#define PIC_SLAVE_OFFSET    0x28u

/* IRQ0 is the timer interrupt (PIT channel 0) */
#define PIC_IRQ_TIMER       0u

/*
 * pic_init - initialize and remap the 8259A PIC.
 *
 * Remaps master PIC to vector base PIC_MASTER_OFFSET and slave PIC to
 * PIC_SLAVE_OFFSET. Masks all IRQs except IRQ0 (timer).
 *
 * Must be called after idt_init().
 */
void pic_init(void);

/*
 * pic_send_eoi - send End Of Interrupt to the PIC chain.
 *
 * irq - the IRQ line that fired (0-15). For IRQ8-IRQ15, EOI is sent to
 * both the slave and master PICs.
 */
void pic_send_eoi(uint32_t irq);

/*
 * pic_mask_all - mask all IRQ lines on both PICs.
 *
 * Useful during kernel shutdown or before switching to APIC mode.
 */
void pic_mask_all(void);

/*
 * pic_unmask_irq - unmask a single IRQ line.
 *
 * irq - 0-15.
 */
void pic_unmask_irq(uint32_t irq);

#endif /* ZAPADA_ARCH_X86_64_PIC_H */


