//===-- crt0.s - Haydn baremetal startup code -----------------*- asm -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Startup code for Haydn baremetal programs.
// This is the _start entry point that the linker uses.
//
// Register conventions (from HaydnCallingConv.td; hard constraint #5):
//   R0  = reserved soft-zero (hardwired to 0, NEVER used as arg or return)
//   R1  = first arg register / i32 return value register
//   R2  = second arg register
//   R13 = SP (Stack Pointer)
//   R14 = FP (Frame Pointer, when used)
//   R15 = LR (Link Register)
//
// Memory layout (defined in haydn.ld):
//   RAM starts at ORIGIN (0x10000 by default)
//   __stack_top is exported by the linker script (top of the .stack region)
//   __bss_start / __bss_end bound the BSS section
//
// Responsibilities (standard C baremetal runtime contract):
//   1. Set SP = __stack_top (NOT a hardcoded address -- program size varies).
//   2. Clear the BSS section to zero ([__bss_start, __bss_end)).
//   3. Call main with argc=0, argv=NULL.
//   4. Halt on return (no OS to return to).
//
//===----------------------------------------------------------------------===//

        .section .text.crt0, "ax", @progbits
        .global _start
        .type _start,@function

_start:
        // --- Step 1: Set up stack pointer from the linker-provided symbol ---
        // SP = __stack_top. Use the HI20/LO16 pair (LUI + ADDI32) that the
        // Haydn backend uses for 32-bit address materialization. The linker
        // resolves both relocations against __stack_top so SP tracks the
        // actual top of the reserved .stack region regardless of program size.
        // (M8-E2E: the previous version hardcoded SP=0x20000, which was only
        // correct for a 64KB-RAM/empty-program layout and drifted as soon as
        // .text/.data grew. See ~/haydn-plans/decisions/D159-m8-end-to-end-pipeline.md.)
        lui     R1, __stack_top
        addi32  SP, R1, __stack_top

        // --- Step 2: Clear BSS ([__bss_start, __bss_end) -> 0) ---
        // Standard C runtime contract: uninitialized globals must read as 0.
        // The previous version skipped this ("programs should handle their
        // own initialization"), which silently broke any program relying on
        // zero-initialized globals/statics. Load both boundary symbols and
        // store zero words until the pointers meet. If __bss_start ==
        // __bss_end (empty BSS), the loop is skipped entirely.
        lui     R2, __bss_start
        addi32  R2, R2, __bss_start
        lui     R3, __bss_end
        addi32  R3, R3, __bss_end
.Lbss_loop:
        BEQ     R2, R3, .Lbss_done      // reached __bss_end -> done
        ST32    R0, R2, 0               // *ptr = 0 (R0 is hardwired zero)
        ADDI32  R2, R2, 4               // ptr += 4
        BEQ     R0, R0, .Lbss_loop      // iterate
.Lbss_done:

        // --- Step 3: Call main (baremetal: argc=0, argv=NULL) ---
        // Per HaydnCallingConv.td: R1 = first arg (argc), R2 = second arg (argv).
        // R0 is the reserved soft-zero register and MUST NOT be used as an arg
        // or return register. main(int argc, char **argv) -> argc in R1, argv
        // in R2. ZERO_GPR is the same pseudo FrameLowering uses to zero R0 in
        // the prologue (semantically move32 rX, r0). (D174.)
        ZERO_GPR R1                     // argc = 0 (R1 = first arg)
        ZERO_GPR R2                     // argv = NULL (R2 = second arg)
        JAL     LR, main                // return address saved in LR

        // --- Step 4: Halt on return ---
        // In baremetal there is nowhere to return to. Per RetCC_Haydn, main's
        // i32 return value is in R1 (NOT R0 — R0 is reserved soft-zero). R1 is
        // preserved here for debugger inspection.
.Lhalt:
        BEQ     R0, R0, .Lhalt          // infinite loop (R0 == 0 always)

        .size _start, . - _start

//===----------------------------------------------------------------------===//
// Optional: default exit handler for programs that call exit()
//===----------------------------------------------------------------------===//

        .section .text.exit, "ax", @progbits
        .global exit
        .type exit,@function

exit:
        // Exit: halt in an infinite loop.
        // Per RetCC_Haydn, the int return/exit code is in R1 (NOT R0 — R0 is
        // the reserved soft-zero register). R1 is preserved for debugger
        // inspection.
.Lexit_halt:
        BEQ     R0, R0, .Lexit_halt     // Halt (R0 == 0 always)

        .size exit, . - exit
