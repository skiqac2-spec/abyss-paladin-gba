@ Minimal GBA startup: header, RAM init, then main()
    .section .crt0, "ax"
    .global _start
    .arm
_start:
    b   rom_start
    .fill 156, 1, 0            @ Nintendo logo area (filled by gbafix / fixrom.py)
    .ascii "ABYSSPALADIN"      @ title (12)
    .ascii "ABPE"              @ game code
    .ascii "01"                @ maker
    .byte 0x96, 0, 0
    .fill 7, 1, 0
    .byte 0                    @ version
    .byte 0                    @ complement (fixed later)
    .byte 0, 0
rom_start:
    mov r0, #0x12              @ IRQ mode stack
    msr cpsr, r0
    ldr sp, =0x03007FA0
    mov r0, #0x1F              @ system mode stack
    msr cpsr, r0
    ldr sp, =0x03007F00
    @ copy .data (IWRAM) from ROM
    ldr r0, =__data_lma
    ldr r1, =__data_start
    ldr r2, =__data_end
1:  cmp r1, r2
    ldrlt r3, [r0], #4
    strlt r3, [r1], #4
    blt 1b
    @ copy .iwram code
    ldr r0, =__iwram_lma
    ldr r1, =__iwram_start
    ldr r2, =__iwram_end
2:  cmp r1, r2
    ldrlt r3, [r0], #4
    strlt r3, [r1], #4
    blt 2b
    @ clear .bss (IWRAM)
    ldr r1, =__bss_start
    ldr r2, =__bss_end
    mov r3, #0
3:  cmp r1, r2
    strlt r3, [r1], #4
    blt 3b
    @ clear .ewram_bss
    ldr r1, =__ewram_bss_start
    ldr r2, =__ewram_bss_end
4:  cmp r1, r2
    strlt r3, [r1], #4
    blt 4b
    ldr r0, =main
    bx  r0
    .pool
