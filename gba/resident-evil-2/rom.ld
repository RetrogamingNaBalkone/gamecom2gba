ENTRY(_start)
SECTIONS {
    . = 0x08000000;
    .text : {
        *(.text.start) *(.text .text.*) *(.rodata .rodata.*)
        . = ALIGN(4);
        assets = .;
    }
    .ewram 0x02000000 (NOLOAD) : {
        __bss_start = .;
        *(.ewram .ewram.* .bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }
    ASSERT(SIZEOF(.ewram) <= 256K, "RE2 work RAM exceeds EWRAM")
    /DISCARD/ : { *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
