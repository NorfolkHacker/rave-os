; Tiny asm stub that becomes the very first bytes of the kernel binary.
; stage2 far-jumps straight to this physical address after switching to
; protected mode, so this has to be real, valid 32-bit code at offset 0 --
; a bare C file has no guaranteed entry point/address without this.
BITS 32
section .text
global _start
extern kmain
extern __bss_start
extern __bss_end

_start:
    ; objcopy -O binary drops .bss from the flat kernel.bin entirely (it
    ; has no file content by definition -- .bss just reserves zeroed
    ; memory), so nothing has zeroed this range before we get here. Every
    ; static/global variable with no explicit initializer (ring buffers,
    ; the IDT table, ...) is relying on this actually being zero, so it
    ; has to happen before kmain touches any of them.
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

    call kmain
.hang:
    hlt
    jmp .hang
