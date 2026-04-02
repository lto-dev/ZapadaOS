/*
 * Zapada - src/kernel/panic.c
 *
 * Kernel panic implementation.
 *
 * On a panic:
 *   1. Print a formatted header with file and line information.
 *   2. Print the message.
 *   3. Disable interrupts and halt permanently.
 *
 * The panic path deliberately avoids any complex formatting or allocations
 * so it works reliably even when the rest of the kernel is in an unknown state.
 */

#include <kernel/panic.h>
#include <kernel/console.h>
#include <kernel/types.h>

/* Helper: write a decimal integer without allocations. */
static void write_int(int value)
{
    char  buf[12];
    int   i   = 10;
    int   neg = 0;

    buf[11] = '\0';

    if (value < 0) {
        neg   = 1;
        value = -value;
    }

    if (value == 0) {
        console_write("0");
        return;
    }

    while (value > 0 && i >= 0) {
        buf[i--] = (char)('0' + (value % 10));
        value /= 10;
    }

    if (neg) {
        buf[i--] = '-';
    }

    console_write(&buf[i + 1]);
}

__attribute__((noreturn))
void kernel_panic(const char *file, int line, const char *message)
{
    /*
     * Disable hardware interrupts immediately to prevent any further
     * corruption of the diagnostic output.
     */
#if defined(__aarch64__)
    __asm__ volatile ("msr daifset, #0xf" ::: "memory");
#else
    __asm__ volatile ("cli");
#endif

    console_write("\n\n");
    console_write("*** KERNEL PANIC ***\n");
    console_write("File    : ");
    console_write(file != NULL ? file : "(unknown)");
    console_write("\n");
    console_write("Line    : ");
    write_int(line);
    console_write("\n");
    console_write("Message : ");
    console_write(message != NULL ? message : "(no message)");
    console_write("\n");
    console_write("System halted.\n");

    /* Disable interrupts and halt permanently. */
    for (;;) {
#if defined(__aarch64__)
        __asm__ volatile ("wfe");
#else
        __asm__ volatile ("cli; hlt");
#endif
    }
}

