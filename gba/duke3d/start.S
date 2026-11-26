.syntax unified
.cpu arm7tdmi
.arm
.section .text.start,"ax"
.global _start
_start:
    b boot
    .space 188
boot:
    ldr sp, =0x03007e00
    mov r0, #0x04000000
    mov r1, #0x80
    strh r1, [r0]           @ forced blank during initialization
    ldr r1, =assets
    mov r2, #0x05000000
    mov r3, #8              @ first 16 BG palette entries
    bl copy_words
    ldr r2, =0x0600f800     @ screen block 31, 32 tiles per row
    mov r3, #0
    mov r4, #20
row:
    mov r5, #30
tile:
    strh r3, [r2], #2
    add r3, r3, #1
    subs r5, r5, #1
    bne tile
    add r2, r2, #4
    subs r4, r4, #1
    bne row
    ldr r1, =assets
    ldr r2, =0x05000200
    mov r3, #8
    bl copy_words
    mov r2, #0x07000000
    mov r3, #128
    mov r4, #0x200
hide_object:
    strh r4, [r2], #8
    subs r3, r3, #1
    bne hide_object
    ldr r1, =play
    bx r1
copy_words:
    ldr r6, [r1], #4
    str r6, [r2], #4
    subs r3, r3, #1
    bne copy_words
    bx lr
.ltorg
.balign 4

.section .rodata
.asciz "SRAM_V113"
