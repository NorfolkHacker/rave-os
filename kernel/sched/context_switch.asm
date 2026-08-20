; void context_switch(uint32_t *save_esp_here, uint32_t new_esp)
;
; Saves the four callee-saved registers (ebx/esi/edi/ebp) onto whichever
; stack is currently active, stores the resulting esp into
; *save_esp_here, switches esp to new_esp, and restores the other
; side's saved registers before ret'ing -- which resumes execution
; wherever that side last called context_switch() itself (or, the
; first time a program stack is used, wherever its stack was primed to
; "return" into -- see scheduler.c's scheduler_activate()). Same code
; serves both directions: it's a symmetric swap, not two routines.
global context_switch
section .text
context_switch:
    push ebp
    push edi
    push esi
    push ebx
    ; 4 pushes = 16 bytes consumed, so the caller's own stack layout
    ; (return addr, then the two arguments) is now 16 bytes higher up.
    mov eax, [esp+20]      ; save_esp_here
    mov [eax], esp
    mov eax, [esp+24]      ; new_esp
    mov esp, eax
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret
