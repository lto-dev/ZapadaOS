#include <kernel/arch/aarch64/exception.h>
#include <kernel/arch/aarch64/uart.h>
#include <kernel/types.h>

/*
 * Vector names indexed by the 0-15 vector table position.
 *
 * Position  EL group         Type
 * --------  ---------------  ----------
 *  0-3      EL1t (SP_EL0)    Sync/IRQ/FIQ/SError
 *  4-7      EL1h (SP_EL1)    Sync/IRQ/FIQ/SError  <- kernel path
 *  8-11     EL0 AArch64      Sync/IRQ/FIQ/SError
 * 12-15     EL0 AArch32      Sync/IRQ/FIQ/SError
 */
static const char *const vector_names[16] = {
    "EL1t Synchronous",    "EL1t IRQ",     "EL1t FIQ",     "EL1t SError",
    "EL1h Synchronous",    "EL1h IRQ",     "EL1h FIQ",     "EL1h SError",
    "EL0-AArch64 Sync",    "EL0-AArch64 IRQ", "EL0-AArch64 FIQ", "EL0-AArch64 SError",
    "EL0-AArch32 Sync",    "EL0-AArch32 IRQ", "EL0-AArch32 FIQ", "EL0-AArch32 SError"
};

/*
 * ESR_EL1 Exception Class (EC) field [31:26].
 * A small lookup table for the most common EC values.
 */
static const char *exception_class_name(uint32_t ec)
{
    switch (ec) {
    case 0x00: return "Unknown";
    case 0x0E: return "Illegal execution state";
    case 0x15: return "SVC (AArch64)";
    case 0x16: return "HVC (AArch64)";
    case 0x17: return "SMC (AArch64)";
    case 0x20: return "Instruction abort (lower EL)";
    case 0x21: return "Instruction abort (same EL)";
    case 0x22: return "PC alignment fault";
    case 0x24: return "Data abort (lower EL)";
    case 0x25: return "Data abort (same EL)";
    case 0x26: return "SP alignment fault";
    case 0x2C: return "FP exception (AArch64)";
    case 0x2F: return "SError interrupt";
    case 0x30: return "Breakpoint (lower EL)";
    case 0x31: return "Breakpoint (same EL)";
    case 0x32: return "Software step (lower EL)";
    case 0x33: return "Software step (same EL)";
    case 0x34: return "Watchpoint (lower EL)";
    case 0x35: return "Watchpoint (same EL)";
    case 0x3C: return "BRK (AArch64)";
    default:   return "(reserved)";
    }
}

void exception_init(void)
{
    uintptr_t vbar = (uintptr_t)exception_vectors;

    __asm__ volatile (
        "msr vbar_el1, %0\n"
        "isb\n"
        :
        : "r"(vbar)
        : "memory"
    );
}

/*
 * aarch64_exception_handler
 *
 * Called from each vector table entry with the exception register values.
 * This function does not return.
 */
void aarch64_exception_handler(
    uint64_t esr,
    uint64_t elr,
    uint64_t far_reg,
    uint64_t spsr,
    uint64_t vector)
{
    uint32_t ec  = (uint32_t)((esr >> 26) & 0x3Fu);
    uint32_t iss = (uint32_t)(esr & 0x1FFFFFFu);

    uart_write("\n*** AArch64 EXCEPTION ***\n");
    uart_write("------------------------------\n");

    uart_write("Vector  : [");
    uart_write_dec(vector);
    uart_write("] ");
    if (vector < 16u) {
        uart_write(vector_names[vector]);
    } else {
        uart_write("(unknown)");
    }
    uart_write("\n");

    uart_write("EC      : ");
    uart_write_hex64((uint64_t)ec);
    uart_write("  (");
    uart_write(exception_class_name(ec));
    uart_write(")\n");

    uart_write("ISS     : ");
    uart_write_hex64((uint64_t)iss);
    uart_write("\n");

    uart_write("ESR_EL1 : ");
    uart_write_hex64(esr);
    uart_write("\n");

    uart_write("ELR_EL1 : ");
    uart_write_hex64(elr);
    uart_write("\n");

    uart_write("FAR_EL1 : ");
    uart_write_hex64(far_reg);
    uart_write("\n");

    uart_write("SPSR_EL1: ");
    uart_write_hex64(spsr);
    uart_write("\n");

    uart_write("------------------------------\n");
    uart_write("System halted.\n");

    for (;;) {
        __asm__ volatile ("wfe");
    }
}
