ENTRY(_start)
SECTIONS {
    . = 0x08000000;
    .text : { *(.text.start) *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4); assets = .; }
    /DISCARD/ : { *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
