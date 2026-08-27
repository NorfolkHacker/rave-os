; kernel/arch/ring3.asm -- the whole ring3 entry/exit boundary in one
; file: enter_ring3() drops CPL0 -> CPL3, syscall_entry is what ring 3
; code calls back into. Hand-written (not GCC's
; __attribute__((interrupt)), the way every other ISR in this codebase
; is written -- see kernel/arch/isr.c's own header comment) because a
; syscall needs register-precise control over arguments/return value
; that attribute doesn't expose; kernel/sched/context_switch.asm
; already establishes the precedent for hand-written asm exactly when
; C can't express the needed register discipline. See
; docs/superpowers/specs/2026-08-27-ring3-syscall-design.md.

extern syscall_dispatch

global enter_ring3
global syscall_entry

section .text

; void enter_ring3(void (*entry)(void), void *user_stack_top)
; Builds the 5-word frame iret expects for a privilege-level change
; and lets it do the CPL0 -> CPL3 switch. Never returns to its caller
; -- there is no return-from-ring3 mechanism in this minimal design;
; this codebase's own use of it (kernel.c's temporary ring3 test)
; ends in a deliberate fault instead.
enter_ring3:
    mov eax, [esp+4]        ; entry
    mov ecx, [esp+8]        ; user_stack_top

    mov dx, 0x20 | 3         ; user data selector, RPL 3
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx

    push dword 0x20 | 3       ; SS
    push ecx                   ; ESP
    pushfd
    pop edx
    or edx, 0x200                ; IF -- ring3 code stays interruptible
    push edx                      ; EFLAGS
    push dword 0x18 | 3            ; CS
    push eax                        ; EIP
    iret

; int 0x80 entry point (installed via idt_set_gate() from kmain(),
; DPL 3 trap gate -- see arch/idt.h's IDT_TYPE_TRAP_GATE_32_DPL3).
; eax = syscall number (in), ebx = argument (in), eax = return value
; (out). Every other GP register is left genuinely clobbered -- not
; part of this minimal ABI's contract.
syscall_entry:
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10           ; kernel data selector -- DS/ES/FS/GS still
    mov ds, ax               ; hold whatever the ring3 caller had
    mov es, ax                ; loaded, and must not be trusted for
    mov fs, ax                 ; kernel-side work even though this
    mov gs, ax                  ; build's user/kernel data segments
                                  ; are numerically identical

    push ebx                ; arg
    push eax                 ; num
    call syscall_dispatch
    add esp, 8                ; eax now holds syscall_dispatch's
                                ; return value -- untouched below

    pop gs
    pop fs
    pop es
    pop ds
    iret
