; Zapada - src/kernel/arch/x86_64/context_switch.asm
;
; Phase 2C: Kernel-mode cooperative context switch for x86_64.
;
; Convention:
;   sched_x86_context_switch(cpu_context_t *prev, cpu_context_t *next)
;     rdi = prev  (pointer to the current thread's cpu_context_t)
;     rsi = next  (pointer to the next thread's cpu_context_t)
;
; Layout of cpu_context_t (see process.h):
;   offset 0:  sp    (uint64_t) - saved kernel RSP
;   offset 8:  ip    (uint64_t) - initial entry point (for first dispatch)
;   offset 16: flags (uint64_t) - saved flags (informational)
;
; Algorithm:
;   1. Push callee-saved registers (System V ABI: rbp, rbx, r12-r15)
;      onto the CURRENT thread's kernel stack. This preserves the
;      registers across the switch from the perspective of the calling
;      C function (sched_context_switch in sched.c).
;   2. Save the current RSP to prev->sp (offset 0).
;   3. Load RSP from next->sp (offset 0).
;   4. Pop callee-saved registers from the NEW thread's kernel stack.
;   5. Return (RET): the return address on the new stack is either:
;      a. The address after the call to sched_x86_context_switch in a
;         thread that previously yielded (normal resume path).
;      b. A thread_entry_trampoline address set up by kstack_init_context
;         for a brand-new thread that has never run.
;
; Callee-saved registers (System V AMD64 ABI):
;   rbp, rbx, r12, r13, r14, r15
;   (rsp is handled explicitly)

global sched_x86_context_switch
global thread_trampoline_x86

section .text
bits 64

sched_x86_context_switch:
    ; Push callee-saved registers onto the CURRENT thread's stack.
    ; Push order: rbp, rbx, r12, r13, r14, r15.
    ; Pop order (below) is reversed: r15, r14, r13, r12, rbx, rbp.
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Save current stack pointer to prev->sp (offset 0 of cpu_context_t)
    mov [rdi], rsp

    ; Load next thread's stack pointer from next->sp (offset 0 of cpu_context_t)
    mov rsp, [rsi]

    ; Restore callee-saved registers from the NEXT thread's stack
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ; Return to the resume point on the next thread's stack.
    ; For a previously-running thread: returns to caller of sched_context_switch.
    ; For a new thread: returns to thread_trampoline_x86 (set by kstack_init_context).
    ret

; -------------------------------------------------------------------------
; thread_trampoline_x86
;
; Entry point for new kernel threads on their first dispatch.
;
; kstack_init_context places the thread's actual entry function address in
; rbx (callee-saved, restored by sched_x86_context_switch).  This trampoline
; calls that function. If the thread function ever returns, the trampoline
; halts the CPU in an infinite loop (no valid return path for kernel threads
; in Phase 2C).
;
; No stack frame setup needed here: the entry function will set up its own
; prologue via its compiler-generated code.
; -------------------------------------------------------------------------
thread_trampoline_x86:
    ; rbx = entry_fn (restored by sched_x86_context_switch)
    call rbx

    ; Thread entry function returned unexpectedly. Halt.
.halt:
    hlt
    jmp .halt

