#ifndef ZAPADA_ARCH_AARCH64_EXCEPTION_H
#define ZAPADA_ARCH_AARCH64_EXCEPTION_H

#include <kernel/types.h>

/*
 * AArch64 exception infrastructure.
 *
 * exception_init  - write the address of exception_vectors to VBAR_EL1.
 *                   Must be called once in kernel_main_aarch64 before
 *                   any code that could take an exception.
 *
 * aarch64_exception_handler - C handler called from each vector entry
 *   (exception_vectors.S). Prints ESR_EL1, ELR_EL1, FAR_EL1, SPSR_EL1
 *   and the vector name, then halts. Does not return.
 *
 * exception_vectors - symbol exported by exception_vectors.S; used by
 *   exception_init to set VBAR_EL1.
 */

/* Defined in exception_vectors.S; address is used as VBAR_EL1 value. */
extern void exception_vectors(void);

void exception_init(void);

void aarch64_exception_handler(
    uint64_t esr,
    uint64_t elr,
    uint64_t far_reg,
    uint64_t spsr,
    uint64_t vector);

#endif /* ZAPADA_ARCH_AARCH64_EXCEPTION_H */

