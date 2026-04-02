#include <kernel/arch/x86_64/idt.h>
#include <kernel/panic.h>
#include <kernel/serial.h>

typedef struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;

extern void *isr_stub_table[32];
extern void  isr_default_stub(void);
extern void  irq0_stub(void);

static idt_entry_t g_idt[256];
static idtr_t      g_idtr;

static const char *g_exception_names[32] = {
    "#DE Divide Error",
    "#DB Debug",
    "NMI",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR Bound Range Exceeded",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "Coprocessor Segment Overrun",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection Fault",
    "#PF Page Fault",
    "Reserved",
    "#MF x87 Floating-Point Exception",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD Floating-Point Exception",
    "#VE Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "#VC VMM Communication Exception",
    "#SX Security Exception",
    "Reserved"
};

static void idt_set_gate(uint8_t vector, void *handler)
{
    uintptr_t    address = (uintptr_t)handler;
    idt_entry_t *entry   = &g_idt[vector];

    entry->offset_low    = (uint16_t)(address & 0xFFFFu);
    entry->selector      = 0x08u;
    entry->ist           = 0;
    entry->type_attr     = 0x8Eu;
    entry->offset_middle = (uint16_t)((address >> 16) & 0xFFFFu);
    entry->offset_high   = (uint32_t)((address >> 32) & 0xFFFFFFFFu);
    entry->reserved      = 0;
}

void idt_init(void)
{
    uint32_t i;

    /* Install exception stubs for CPU vectors 0-31. */
    for (i = 0; i < 32; i++) {
        idt_set_gate((uint8_t)i, isr_stub_table[i]);
    }

    /*
     * Install a default stub for vectors 32-255.
     * These are hardware IRQ vectors. Without this, any spurious interrupt
     * on these vectors would cause a not-present gate fault, escalating to
     * a double fault and then a silent triple fault / QEMU reset.
     *
     * isr_default_stub disables interrupts, calls isr_default_handler which
     * emits a diagnostic and panics, then loops on hlt.
     */
    for (i = 32; i < 256; i++) {
        idt_set_gate((uint8_t)i, (void *)isr_default_stub);
    }

    /*
     * Phase 2C: install the timer IRQ handler at vector 0x20 (IRQ0).
     * This overrides the default stub installed by the loop above.
     * The PIT fires at the configured Hz; irq0_stub sends EOI and calls
     * timer_tick_handler() to advance the scheduler tick count.
     */
    idt_set_gate(0x20u, (void *)irq0_stub);

    g_idtr.limit = (uint16_t)(sizeof(g_idt) - 1);
    g_idtr.base  = (uint64_t)(uintptr_t)&g_idt[0];

    __asm__ volatile ("lidt %0" : : "m"(g_idtr) : "memory");
}

void isr_exception_handler(isr_frame_t *frame)
{
    const char *name = "Unknown Exception";

    if (frame->vector < 32) {
        name = g_exception_names[frame->vector];
    }

    serial_write("\n");
    serial_write("Exception         : ");
    serial_write(name);
    serial_write("\n");
    serial_write("Vector            : ");
    serial_write_dec(frame->vector);
    serial_write("\n");
    serial_write("Error code        : ");
    serial_write_hex64(frame->error_code);
    serial_write("\n");
    serial_write("RIP               : ");
    serial_write_hex64(frame->rip);
    serial_write("\n");
    serial_write("CS                : ");
    serial_write_hex64(frame->cs);
    serial_write("\n");
    serial_write("RFLAGS            : ");
    serial_write_hex64(frame->rflags);
    serial_write("\n");

    PANIC("Unhandled CPU exception");
}

void isr_default_handler(void)
{
    serial_write("\nUnexpected interrupt on vector >= 32\n");
    PANIC("Unexpected interrupt - no handler installed for this vector");
}
