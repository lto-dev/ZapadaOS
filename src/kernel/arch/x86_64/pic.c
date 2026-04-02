/*
 * Zapada - src/kernel/arch/x86_64/pic.c
 *
 * Intel 8259A PIC initialization and control.
 *
 * Reference: Intel 8259A datasheet; OSDev wiki "8259 PIC".
 *
 * PIC I/O ports:
 *   Master: command = 0x20, data = 0x21
 *   Slave:  command = 0xA0, data = 0xA1
 *
 * Initialization sequence (ICW1-ICW4):
 *   ICW1 = 0x11  (ICW4 needed, cascade mode, edge-triggered)
 *   ICW2 = vector offset for IRQ0 on master, IRQ8 on slave
 *   ICW3 = cascade identity (master: slave on IRQ2; slave: ID=2)
 *   ICW4 = 0x01  (8086 mode)
 */

#include <kernel/arch/x86_64/pic.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/console.h>
#include <kernel/types.h>

/* ---------------------------------------------------------------------- */
/* PIC I/O port addresses                                                  */
/* ---------------------------------------------------------------------- */

#define PIC_MASTER_CMD    0x20u
#define PIC_MASTER_DATA   0x21u
#define PIC_SLAVE_CMD     0xA0u
#define PIC_SLAVE_DATA    0xA1u

/* PIC commands */
#define PIC_CMD_EOI       0x20u   /* End of Interrupt */
#define PIC_ICW1_INIT     0x10u   /* Initialization flag */
#define PIC_ICW1_ICW4     0x01u   /* ICW4 will be sent */
#define PIC_ICW4_8086     0x01u   /* 8086/88 mode */

/* Small I/O delay: write to a throwaway port to let slow PIC settle */
static void io_wait(void)
{
    outb(0x80u, 0u);
}

/* ---------------------------------------------------------------------- */
/* pic_init                                                                */
/* ---------------------------------------------------------------------- */

void pic_init(void)
{
    uint8_t master_mask;
    uint8_t slave_mask;

    /* Save existing masks in case we need them later (not used now) */
    master_mask = inb(PIC_MASTER_DATA);
    slave_mask  = inb(PIC_SLAVE_DATA);
    (void)master_mask;
    (void)slave_mask;

    /* ICW1: begin initialization sequence (cascade, ICW4 needed) */
    outb(PIC_MASTER_CMD, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();
    outb(PIC_SLAVE_CMD,  PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();

    /* ICW2: vector base offsets */
    outb(PIC_MASTER_DATA, (uint8_t)PIC_MASTER_OFFSET);
    io_wait();
    outb(PIC_SLAVE_DATA,  (uint8_t)PIC_SLAVE_OFFSET);
    io_wait();

    /* ICW3: cascade identity */
    outb(PIC_MASTER_DATA, 0x04u);   /* Slave PIC is on IRQ2 (bit 2) */
    io_wait();
    outb(PIC_SLAVE_DATA,  0x02u);   /* Slave ID = 2 */
    io_wait();

    /* ICW4: 8086 mode */
    outb(PIC_MASTER_DATA, PIC_ICW4_8086);
    io_wait();
    outb(PIC_SLAVE_DATA,  PIC_ICW4_8086);
    io_wait();

    /*
     * Mask all IRQs on both PICs.
     * Phase 2C then individually unmasks IRQ0 (timer) via pic_unmask_irq().
     * All other IRQs remain masked until drivers are added.
     */
    outb(PIC_MASTER_DATA, 0xFFu);
    outb(PIC_SLAVE_DATA,  0xFFu);

    console_write("PIC             : remapped master=0x20 slave=0x28, all IRQs masked\n");
}

/* ---------------------------------------------------------------------- */
/* pic_send_eoi                                                            */
/* ---------------------------------------------------------------------- */

void pic_send_eoi(uint32_t irq)
{
    if (irq >= 8u) {
        outb(PIC_SLAVE_CMD, PIC_CMD_EOI);
    }
    outb(PIC_MASTER_CMD, PIC_CMD_EOI);
}

/* ---------------------------------------------------------------------- */
/* pic_mask_all                                                            */
/* ---------------------------------------------------------------------- */

void pic_mask_all(void)
{
    outb(PIC_MASTER_DATA, 0xFFu);
    outb(PIC_SLAVE_DATA,  0xFFu);
}

/* ---------------------------------------------------------------------- */
/* pic_unmask_irq                                                          */
/* ---------------------------------------------------------------------- */

void pic_unmask_irq(uint32_t irq)
{
    uint16_t port;
    uint8_t  mask;

    if (irq < 8u) {
        port = PIC_MASTER_DATA;
    } else {
        port = PIC_SLAVE_DATA;
        irq -= 8u;
    }

    mask = inb(port);
    mask &= (uint8_t)(~(1u << irq));
    outb(port, mask);
}

