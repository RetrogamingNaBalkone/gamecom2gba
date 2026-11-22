ENTRY(_start)
SECTIONS {
    . = 0x08000000;
    .text : { *(.text.start) *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4); assets = .; }
    .iwram 0x03000000 (NOLOAD) : {
        KEEP(*(.iwram.position)) KEEP(*(.iwram.encounter))
        KEEP(*(.iwram.weapon)) KEEP(*(.iwram.inventory))
    }
    .ewram 0x02000000 (NOLOAD) : { *(.ewram) }
    /DISCARD/ : { *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
