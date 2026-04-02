; Zapada - src/boot/boot.asm
;
; Multiboot2 header and 32-bit to 64-bit long mode transition.
;
; Entry contract from GRUB2:
;   EAX = 0x36D76289  (Multiboot2 bootloader magic)
;   EBX = Physical address of Multiboot2 information structure
;   CS  = 32-bit protected mode code segment
;   Interrupts disabled
;
; This code sets up:
;   - 2MB identity-mapped page tables for the first 1GB
;   - A minimal boot GDT with a 64-bit code segment
;   - Long mode (PAE + EFER.LME + paging)
;   - A 16 KiB initial stack
;
; After setup, control is transferred to kernel_main(uint32_t mb_magic, void* mb_info).
;
; Note: Stage 1.1 uses a lower-half identity-mapped kernel (virtual = physical).
;       Higher-half mapping is planned for a later stage.
; ------------------------------------------------------------------------------

global _start
extern kernel_main

; Multiboot2 magic values
MB2_MAGIC_HEADER  equ 0xE85250D6
MB2_ARCH_X86      equ 0
MB2_MAGIC_LOADER  equ 0x36D76289

; Page entry flags
PAGE_PRESENT equ (1 << 0)
PAGE_WRITE   equ (1 << 1)
PAGE_HUGE    equ (1 << 7)   ; 2MB or 1GB page

; ------------------------------------------------------------------------------
; Multiboot2 header
; Must be 64-bit aligned, within the first 32KB of the OS image.
; ------------------------------------------------------------------------------
section .multiboot2
align 8
mb2_header:
    dd MB2_MAGIC_HEADER
    dd MB2_ARCH_X86
    dd mb2_header_end - mb2_header
    dd -(MB2_MAGIC_HEADER + MB2_ARCH_X86 + (mb2_header_end - mb2_header))
    ; End tag
    dw 0        ; type = 0 (end)
    dw 0        ; flags
    dd 8        ; size
mb2_header_end:

; ------------------------------------------------------------------------------
; Boot GDT
; Minimal descriptor table for the transition to 64-bit mode.
; Loaded while still in 32-bit protected mode; the GDTR base is a 32-bit
; physical address, which is valid because the GDT lives below 4GB.
; ------------------------------------------------------------------------------
section .boot.data
align 8
boot_gdt:
    ; [0x00] Null descriptor
    dq 0
    ; [0x08] 64-bit code segment, ring 0
    ;   Base=0, Limit=0xFFFFF, Access=0x9A (present, DPL=0, S=1, execute, read)
    ;   Flags=0xA (granularity=1, D/B=0, L=1)
    dq 0x00AF9A000000FFFF
    ; [0x10] 64-bit data segment, ring 0
    ;   Access=0x92 (present, DPL=0, S=1, data, read/write)
    ;   Flags=0xC (granularity=1, D/B=1, L=0 -- data ignores L bit)
    dq 0x00CF92000000FFFF
boot_gdt_end:

boot_gdtr:
    dw boot_gdt_end - boot_gdt - 1     ; limit
    dd boot_gdt                         ; base (32-bit physical)

; ------------------------------------------------------------------------------
; Early page tables
; Three 4KB tables placed in BSS, zeroed at boot, then filled by setup code.
;   pml4_table[0] -> pdpt_table
;   pdpt_table[0] -> pd_table
;   pd_table[0..511] -> 2MB identity pages (covers first 1GB)
;
; Declared as 'nobits' so NASM treats this as a BSS section (no file content).
; ------------------------------------------------------------------------------
section .boot.bss nobits
align 4096
pml4_table: resb 4096
pdpt_table:  resb 4096
pd_table:    resb 4096

; ------------------------------------------------------------------------------
; Initial kernel stack (128 KiB)
;
; The interpreter can recurse up to CLR_MAX_CALL_DEPTH (16) levels; each frame
; places ~2 KiB on the C stack (eval_stack_t, clr_frame_t, local buffers, etc.).
; 16 KiB was insufficient; 64 KiB provides comfortable headroom.
; ------------------------------------------------------------------------------
align 16
stack_bottom: resb 131072
stack_top:

; ==============================================================================
; 32-bit boot code
; Runs in protected mode, sets up long mode, then jumps to 64-bit entry.
; ==============================================================================
section .boot.text
bits 32

_start:
    ; Interrupts are already disabled by GRUB. Keep them off.
    cli

    ; Preserve Multiboot2 info pointer from EBX (scratch-safe in 32-bit mode).
    ; EAX holds MB2_MAGIC_LOADER (checked below); EDI will carry it into 64-bit.
    mov edi, ebx        ; save info pointer
    mov esi, eax        ; save magic

    ; Set up the early stack before any function calls.
    mov esp, stack_top

    ; Verify the Multiboot2 magic number.
    cmp esi, MB2_MAGIC_LOADER
    jne .halt_no_multiboot

    ; Zero the page tables (BSS may not be zeroed by the bootloader).
    call zero_page_tables

    ; Build identity page tables for the first 1GB.
    call setup_identity_tables

    ; Load the boot GDT (needed for the far jump to the 64-bit code segment).
    lgdt [boot_gdtr]

    ; Enable Physical Address Extension (required before activating long mode).
    mov eax, cr4
    or  eax, (1 << 5)   ; CR4.PAE = 1
    mov cr4, eax

    ; Point CR3 at our PML4 table.
    mov eax, pml4_table
    mov cr3, eax

    ; Set EFER.LME to enable long mode.
    mov ecx, 0xC0000080 ; IA32_EFER MSR
    rdmsr
    or  eax, (1 << 8)   ; EFER.LME = 1
    wrmsr

    ; Enable paging (CR0.PG = 1). This activates long mode compatibility mode.
    ; Protected mode bit CR0.PE is already set by GRUB.
    mov eax, cr0
    or  eax, (1 << 31)  ; CR0.PG = 1
    mov cr0, eax

    ; Far jump into the 64-bit code segment (GDT index 1, offset 0x08).
    ; This completes the transition to 64-bit long mode.
    jmp 0x08:long_mode_entry

.halt_no_multiboot:
    ; Invalid bootloader. Hang safely.
    hlt
    jmp .halt_no_multiboot

; ------------------------------------------------------------------------------
; zero_page_tables
; Zeroes all three page tables (3 * 4096 bytes = 3072 dwords).
; ------------------------------------------------------------------------------
zero_page_tables:
    push edi
    mov  edi, pml4_table
    mov  ecx, (3 * 4096) / 4
    xor  eax, eax
    rep  stosd
    pop  edi
    ret

; ------------------------------------------------------------------------------
; setup_identity_tables
; Maps virtual 0x0 -> physical 0x0 in 2MB pages for the first 1GB.
;   PML4[0]      = pdpt_table | PRESENT | WRITE
;   PDPT[0]      = pd_table   | PRESENT | WRITE
;   PD[0..511]   = (n * 2MB)  | PRESENT | WRITE | HUGE
; ------------------------------------------------------------------------------
setup_identity_tables:
    ; PML4[0] -> pdpt_table
    mov eax, pdpt_table
    or  eax, PAGE_PRESENT | PAGE_WRITE
    mov dword [pml4_table + 0], eax
    mov dword [pml4_table + 4], 0

    ; PDPT[0] -> pd_table
    mov eax, pd_table
    or  eax, PAGE_PRESENT | PAGE_WRITE
    mov dword [pdpt_table + 0], eax
    mov dword [pdpt_table + 4], 0

    ; PD[0..511]: each entry maps a 2MB physical page starting at n * 0x200000.
    xor ecx, ecx
.fill_pd:
    ; Physical base for this 2MB page = ecx * 0x200000.
    mov eax, 0x00200000
    mul ecx                     ; EAX = ecx * 2MB  (EDX = 0 for ecx < 2048)
    or  eax, PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE
    ; Write lower 32 bits of the 64-bit page directory entry.
    mov dword [pd_table + ecx * 8],     eax
    ; Write upper 32 bits (physical address stays in first 4GB, so EDX = 0).
    mov dword [pd_table + ecx * 8 + 4], edx

    inc ecx
    cmp ecx, 512
    jl  .fill_pd

    ret

; ==============================================================================
; 64-bit long mode entry point
; Arrived here via far jump from 32-bit boot code.
; Sets data segments, restores MB2 arguments, then calls kernel_main.
; ==============================================================================
section .text
bits 64

long_mode_entry:
    ; Load 64-bit data segments.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; In x86-64 SysV ABI:
    ;   RDI = first argument  (Multiboot2 magic -- from ESI saved above)
    ;   RSI = second argument (Multiboot2 info pointer -- from EDI saved above)
    ;
    ; After the 32-bit MOV EDI, EBX and MOV ESI, EAX, these values were placed
    ; in the lower 32 bits of RDI and RSI. The upper 32 bits were zeroed by the
    ; 32-bit write. Swap them into the correct SysV argument registers.
    mov  eax, edi           ; EAX = info pointer  (was in EDI in 32-bit)
    mov  ecx, esi           ; ECX = magic         (was in ESI in 32-bit)
    mov  edi, ecx           ; RDI = magic (first arg)
    mov  esi, eax           ; RSI = info pointer (second arg)

    ; Call kernel_main(uint32_t mb_magic, void* mb_info).
    ; RSP is currently the boot stack physical address, which is valid because
    ; we are identity-mapped (virtual = physical).
    call kernel_main

    ; kernel_main should not return. Halt if it does.
.halt:
    hlt
    jmp .halt

