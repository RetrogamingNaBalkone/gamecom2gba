ENTRY(_start)
SECTIONS {
    . = 0x08000000;
    .text : {
        *(.text.start) *(EXCLUDE_FILE(*sm8500.o *gamecom.o *pcm_isr.o) .text EXCLUDE_FILE(*sm8500.o *gamecom.o *pcm_isr.o) .text.*) *(.rodata .rodata.*)
        . = ALIGN(4);
    }
    .iwram 0x03000000 : AT(ADDR(.text) + SIZEOF(.text)) {
        __iwram_start = .;
        *(.iwram .iwram.*) *sm8500.o(.text .text.*) *gamecom.o(.text .text.*) *pcm_isr.o(.text .text.*) *(.data .data.*)
        . = ALIGN(4);
        __iwram_end = .;
    }
    __iwram_load = LOADADDR(.iwram);
    assets = __iwram_load + SIZEOF(.iwram);
    .ewram 0x02000000 (NOLOAD) : {
        __bss_start = .;
        *(.ewram .ewram.* .bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }
    ASSERT(SIZEOF(.ewram) <= 256K, "work RAM exceeds EWRAM")
    ASSERT(SIZEOF(.iwram) <= 28K, "IWRAM code too large")
    /DISCARD/ : { *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
