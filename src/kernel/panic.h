/*
 * Zapada - src/kernel/panic.h
 *
 * Kernel panic interface.
 *
 * A panic is an unrecoverable fatal condition. The panic path must:
 *   1. Output a human-readable message on the serial console.
 *   2. Halt the CPU permanently.
 *
 * This is the most critical diagnostic path in the kernel. It must function
 * even when most other subsystems are uninitialized or broken.
 */

#ifndef ZAPADA_PANIC_H
#define ZAPADA_PANIC_H

/*
 * PANIC(msg) - Print a diagnostic message and halt.
 *
 * Includes the source file name and line number in the output.
 * Does not return.
 */
#define PANIC(msg)  kernel_panic(__FILE__, __LINE__, (msg))

/*
 * kernel_panic - Low-level panic implementation.
 *
 * Not intended to be called directly. Use the PANIC() macro.
 * Does not return.
 */
#ifdef __cplusplus
extern "C" {
#endif

__attribute__((noreturn))
void kernel_panic(const char *file, int line, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* ZAPADA_PANIC_H */


