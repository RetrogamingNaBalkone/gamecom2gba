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
    strh r1, [r0]
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
clear_bss:
    cmp r0, r1
    strlo r2, [r0], #4
    blo clear_bss
    ldr r1, =play
    bx r1
.ltorg
.balign 4

.section .rodata
.asciz "SRAM_V113"

@ ARM EABI unsigned quotient (r0) and remainder (r1) for freestanding C.
.section .text,"ax"
.arm
.global __aeabi_uidiv
.global __aeabi_uidivmod
__aeabi_uidiv:
__aeabi_uidivmod:
    mov r2, r0
    mov r0, #0
    mov r3, #1
    cmp r1, #0
    beq div_done
div_align:
    cmp r1, r2, lsr #1
    movls r1, r1, lsl #1
    movls r3, r3, lsl #1
    bls div_align
div_bit:
    cmp r2, r1
    subhs r2, r2, r1
    orrhs r0, r0, r3
    mov r1, r1, lsr #1
    movs r3, r3, lsr #1
    bne div_bit
div_done:
    mov r1, r2
    bx lr
