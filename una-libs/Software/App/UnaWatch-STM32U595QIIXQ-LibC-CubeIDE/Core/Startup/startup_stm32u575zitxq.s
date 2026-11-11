.syntax unified
.cpu cortex-m33
.fpu softvfp
.thumb

.global g_pfnVectors
.global Reset_Handler

.word _sidata
.word _sdata
.word _edata
.word _sbss
.word _ebss

.section .isr_vector, "a", %progbits
.type g_pfnVectors, %object

g_pfnVectors:
    .word Reset_Handler  /* тільки Reset_Handler, без _estack */

.size g_pfnVectors, .-g_pfnVectors

.section .text.Reset_Handler
.weak Reset_Handler
.type Reset_Handler, %function

Reset_Handler:
    /* Немає ldr sp, =_estack !!! */

    /* Копіюємо .data з flash в RAM */
    movs r1, #0
    b LoopCopyDataInit

CopyDataInit:
    ldr r3, =_sidata
    ldr r3, [r3, r1]
    str r3, [r0, r1]
    adds r1, r1, #4

LoopCopyDataInit:
    ldr r0, =_sdata
    ldr r3, =_edata
    adds r2, r0, r1
    cmp r2, r3
    bcc CopyDataInit

    /* Очищуємо .bss */
    ldr r2, =_sbss
    b LoopFillZerobss

FillZerobss:
    movs r3, #0
    str r3, [r2], #4

LoopFillZerobss:
    ldr r3, =_ebss
    cmp r2, r3
    bcc FillZerobss

    /* Викликаємо конструктори (для C++) */
    bl __libc_init_array

    /* Після main() — повернення у бутлоадер */
    bx lr

.size Reset_Handler, .-Reset_Handler
