OUTPUT_FORMAT("binary")
ENTRY(_start)
SECTIONS {
    . = 0;
    .text   : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    __bss_start = .;
    .bss    : { *(.bss .bss.*) *(COMMON) }
    __bss_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
    ASSERT(__bss_end < 0xF000, "kernel grande demais para o segmento de 64KB")
}
