global isr_stub_table
global isr_default_stub
global irq0_stub

extern isr_exception_handler
extern isr_default_handler
extern irq0_timer_c_handler

section .text
bits 64

%macro PUSH_REGS 0
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_REGS 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

%macro ISR_NO_ERROR 1
isr_stub_%1:
    push qword 0
    push qword %1
    jmp isr_common_stub
%endmacro

%macro ISR_WITH_ERROR 1
isr_stub_%1:
    push qword %1
    jmp isr_common_stub
%endmacro

isr_common_stub:
    cld
    PUSH_REGS
    mov rdi, rsp
    call isr_exception_handler

    POP_REGS
    add rsp, 16
    iretq

; isr_default_stub - handler for unexpected interrupts on vectors 33-255.
; Hardware IRQs do not push an error code. This stub just disables interrupts,
; calls the C handler, and loops on hlt. It does not attempt to return.
isr_default_stub:
    cli
    call isr_default_handler
.loop:
    hlt
    jmp .loop

; irq0_stub - Phase 2C timer interrupt handler for IRQ0 (vector 0x20).
;
; Hardware does NOT push an error code for IRQs. The CPU auto-pushes
; RFLAGS, CS, and RIP onto the current stack before jumping here. This
; stub saves all GP registers, calls the C handler (which sends EOI and
; dispatches the timer tick), restores GP registers, and returns via IRETQ.
irq0_stub:
    cld
    PUSH_REGS
    call irq0_timer_c_handler
    POP_REGS
    iretq

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_WITH_ERROR 8
ISR_NO_ERROR 9
ISR_WITH_ERROR 10
ISR_WITH_ERROR 11
ISR_WITH_ERROR 12
ISR_WITH_ERROR 13
ISR_WITH_ERROR 14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_WITH_ERROR 17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_WITH_ERROR 21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_WITH_ERROR 29
ISR_WITH_ERROR 30
ISR_NO_ERROR 31

section .rodata
align 8
isr_stub_table:
    dq isr_stub_0
    dq isr_stub_1
    dq isr_stub_2
    dq isr_stub_3
    dq isr_stub_4
    dq isr_stub_5
    dq isr_stub_6
    dq isr_stub_7
    dq isr_stub_8
    dq isr_stub_9
    dq isr_stub_10
    dq isr_stub_11
    dq isr_stub_12
    dq isr_stub_13
    dq isr_stub_14
    dq isr_stub_15
    dq isr_stub_16
    dq isr_stub_17
    dq isr_stub_18
    dq isr_stub_19
    dq isr_stub_20
    dq isr_stub_21
    dq isr_stub_22
    dq isr_stub_23
    dq isr_stub_24
    dq isr_stub_25
    dq isr_stub_26
    dq isr_stub_27
    dq isr_stub_28
    dq isr_stub_29
    dq isr_stub_30
    dq isr_stub_31
