/* Fixed load address -- must match PROGRAM_LOAD_ADDR in kernel/kernel.c's
   program_load_and_run(). See docs/superpowers/specs/2026-08-29-loadable-
   program-design.md for why this is fixed rather than relocatable. */
ENTRY(_start)
SECTIONS {
    . = 0x00200000;
    .text   : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) *(COMMON) }
    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note.*) }
}
