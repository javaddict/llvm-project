# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck --check-prefix=ASM %s
# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck --check-prefix=PRINT %s
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o | FileCheck --check-prefix=CARRY %s

# Role: object — parser + printer + emitter for MCSpecifierExpr %hi12/%lo20/%pc_lo20.

# REGRESSION TEST: Haydn %hi12/%lo20/%pc_lo20 specifier expressions.
#
# Parser accepts %hi12/%lo20/%pc_lo20(expr) as MCSpecifierExpr. Printer is
# symmetric. Emitter maps specifier → FIXUP_HAYDN_HI12 / LO20 / PC_LO20;
# a bare symbol still uses the opcode default (LUI→HI12, ADDI32_W→LO20).
# Specifier wins when it disagrees with the opcode (lui %lo20 → R_HAYDN_LO20).
#
# Carry rounding: LUI hi12 is rounded by half simm20 (Val+0x80000)>>20 so
# ADDI32_W sign-extend reconstructs Val. 0x180000 → hi=2, lo=-524288.
# If rounding regresses, hi=1 and lo=+524288, which sign-extends to the
# wrong 32-bit value. Constants are resolved at MC (no linker).
#
# Test design: constants for carry; extern `foo` for reloc kinds; one
# specifier-vs-opcode override; one bare-symbol pair.

    .text

# ASM: FIXUP_HAYDN_HI12
# ASM: FIXUP_HAYDN_LO20
# PRINT: %hi12(1572864)
# PRINT: %lo20(1572864)
# CARRY: lui{{.*}} 2
# CARRY: addi32{{(_w)?}}{{.*}}-524288
    lui r1, %hi12(1572864)
    addi32_w r1, r1, %lo20(1572864)

# ASM: FIXUP_HAYDN_HI12
# ASM: FIXUP_HAYDN_LO20
# ASM: FIXUP_HAYDN_PC_LO20
# PRINT: %hi12(foo)
# PRINT: %lo20(foo)
# PRINT: %pc_lo20(foo)
    lui r2, %hi12(foo)
    addi32_w r2, r2, %lo20(foo)
    addi32_w r3, r3, %pc_lo20(foo)

# Specifier overrides LUI's HI12 default.
# PRINT: %lo20(foo)
    lui r4, %lo20(foo)

# Bare symbol: opcode default (HI12 on LUI, LO20 on ADDI32_W).
    lui r5, foo
    addi32_w r5, r5, foo

# RELOCS: Relocations [
# RELOCS: R_HAYDN_HI12 foo
# RELOCS: R_HAYDN_LO20 foo
# RELOCS: R_HAYDN_PC_LO20 foo
# RELOCS: R_HAYDN_LO20 foo
# RELOCS: R_HAYDN_HI12 foo
# RELOCS: R_HAYDN_LO20 foo
# RELOCS: ]
