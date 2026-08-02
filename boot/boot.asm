; Rave-OS stage-1 boot sector.
; BIOS loads this at physical address 0x7C00 and jumps to it in 16-bit real mode.
BITS 16
ORG 0x7C00

start:
    cli                 ; no interrupts while segment/stack setup is mid-flight
    xor ax, ax
    mov ds, ax          ; data segment = 0
    mov es, ax          ; extra segment = 0
    mov ss, ax          ; stack segment = 0
    mov sp, 0x7C00      ; stack grows down from right below us
    sti

    mov si, msg

.print_char:
    lodsb               ; al = [ds:si], si++
    or al, al
    jz .hang            ; NUL terminator -> done
    mov ah, 0x0E        ; BIOS teletype-output function
    mov bh, 0x00        ; page number
    mov bl, 0x07        ; text attribute: light grey on black
    int 0x10            ; BIOS video services interrupt
    jmp .print_char

.hang:
    cli
    hlt
    jmp .hang           ; hlt can be woken by NMI/debug traps; loop to stay stopped

msg db 'Rave-OS boot sector alive.', 0

times 510 - ($ - $$) db 0   ; pad to 510 bytes
dw 0xAA55                   ; boot signature; BIOS checks for this at bytes 511-512
