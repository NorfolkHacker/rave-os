; Rave-OS stage-1 boot sector.
; BIOS loads this at physical address 0x7C00 and jumps to it in 16-bit real mode.
; Job: say hello, load stage2 off disk, then hand off control to it.
BITS 16
ORG 0x7C00

STAGE2_SEGMENT equ 0x0000
STAGE2_OFFSET  equ 0x8000

; STAGE2_SECTORS is normally passed in by boot/Makefile via `nasm -D`,
; computed from stage2.bin's actual measured size -- this used to be a
; constant hand-copied here and kept in sync with stage2.asm/kernel/
; Makefile by hand (a real, repeatedly-hit source of bugs; see
; docs/BUILD_LOG.md's consolidation entry). The fallback below only
; matters if this file is ever assembled directly, outside the Makefile.
%ifndef STAGE2_SECTORS
STAGE2_SECTORS equ 2
%endif

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl   ; BIOS passes the boot drive number in dl on entry; save before it's clobbered

    mov si, msg_loading
    call print_string

    mov ah, 0x02            ; BIOS function: read sectors (CHS addressing)
    mov al, STAGE2_SECTORS
    mov ch, 0                ; cylinder 0
    mov cl, 2                ; sector 2 (1-indexed; sector 1 is this boot sector)
    mov dh, 0                ; head 0
    mov dl, [boot_drive]
    mov bx, STAGE2_OFFSET    ; ES:BX destination, ES already 0
    int 0x13
    jc disk_error

    jmp STAGE2_SEGMENT:STAGE2_OFFSET

disk_error:
    mov si, msg_disk_error
    call print_string
.hang:
    cli
    hlt
    jmp .hang

; si -> null-terminated string; prints via BIOS teletype, clobbers ax/bx/si
print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp print_string
.done:
    ret

boot_drive db 0
msg_loading db 'Rave-OS: loading stage2...', 13, 10, 0
msg_disk_error db 'Disk read error!', 0

times 510 - ($ - $$) db 0
dw 0xAA55
