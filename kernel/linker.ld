ENTRY(_start)

SECTIONS
{
    /* Must match KERNEL_LOAD_ADDR in boot/stage2.asm -- that's the physical
       address stage2 far-jumps to after enabling protected mode. */
    . = 0x10000;

    .text   : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) *(COMMON) }

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note.*) }
}
