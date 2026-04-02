/*
 * Zapada - src/kernel/arch/x86_64/idt.h
 *
 * IDT management and ISR frame layout for x86_64.
 *
 * Phase 2C additions:
 *   - user_isr_frame_t: extended frame for ring-3 -> ring-0 privilege
 *     transitions. The hardware additionally pushes RSP and SS when the
 *     interrupt occurs in user mode (CPL=3 -> CPL=0).
 */

#ifndef ZAPADA_ARCH_X86_64_IDT_H
#define ZAPADA_ARCH_X86_64_IDT_H

#include <kernel/types.h>

/*
 * isr_frame_t - kernel-mode ISR frame (no privilege change).
 *
 * Layout in memory from the stack pointer at ISR entry (lowest address first):
 *   [software pushed by PUSH_REGS macro in isr.asm]
 *   r15, r14, r13, r12, r11, r10, r9, r8, rsi, rdi, rbp, rdx, rcx, rbx, rax
 *   vector, error_code  (software or hardware)
 *   [hardware pushed on interrupt, no privilege change]
 *   rip, cs, rflags
 */
typedef struct isr_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
} isr_frame_t;

/*
 * user_isr_frame_t - ISR frame when interrupted in user mode (ring-3).
 *
 * When the CPU takes an interrupt while executing at ring-3 (CPL=3 to CPL=0),
 * it additionally pushes the user-mode SS and RSP BEFORE pushing RFLAGS.
 *
 * This struct extends isr_frame_t with the two extra hardware-pushed fields.
 * To access user-mode frame from an ISR handler:
 *
 *   if ((frame->cs & 3) == 3) {
 *       user_isr_frame_t *uframe = (user_isr_frame_t *)frame;
 *       // uframe->rsp_user and uframe->ss_user are valid
 *   }
 */
typedef struct user_isr_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    /* Present only on ring-3 -> ring-0 transitions: */
    uint64_t rsp_user;  /* user-mode stack pointer at time of interrupt */
    uint64_t ss_user;   /* user-mode stack segment selector             */
} user_isr_frame_t;

void idt_init(void);
void isr_exception_handler(isr_frame_t *frame);

#endif /* ZAPADA_ARCH_X86_64_IDT_H */


