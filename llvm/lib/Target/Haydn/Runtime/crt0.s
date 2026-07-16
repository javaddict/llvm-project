// Haydn baremetal startup code (crt0.s)
// Sets up stack pointer and calls main, then loops.
// Register convention: R0=soft-zero, R1-R7=args+returns, SP=R13, FP=R14, LR=R15
// Register asm names: r0-r12, sp (R13), fp (R14), lr (R15)
// The linker script defines __stack_top at the top of RAM (0x10000 = 64KB).

    .section .text.startup, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    // Set up stack pointer to top of RAM (0x10000 = 64KB).
    // LUI loads bits [31:16], ADDI32 adds sign-extended bits [15:0].
    lui     sp, 0x0001           // SP upper = 0x00010000
    addi32  sp, sp, 0            // SP lower = 0x0000

    // Clear frame pointer (R0 is hardwired to zero)
    move32  fp, r0               // FP = 0

    // Set up args for main: argc=0, argv=NULL
    move32  r1, r0               // argc = 0 (R1 = first arg)
    move32  r2, r0               // argv = NULL (R2 = second arg)

    // Call main
    jal     lr, main             // LR = return address, jump to main

    // After main returns, store return value for debugging.
    // R1 holds main's return value (i32).
    lui     r12, 0x0000          // Address upper 16 bits = 0x0000
    addi32  r12, r12, 0x1000     // Address lower 16 bits = 0x1000
    st32    r1, r12, 0           // Store return value at 0x1000

    // Infinite halt loop: beqz r0, _halt always branches (r0 == 0)
_halt:
    beqz    r0, _halt

    .size _start, . - _start
