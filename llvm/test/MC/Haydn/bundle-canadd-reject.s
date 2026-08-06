# RUN: not llvm-mc -triple=haydn-unknown-elf %s 2>&1 | FileCheck %s
# REQUIRES: haydn-registered-target
# Format E96 cutover residual: FileCheck/idle-pad/reloc geometry still open (GE96-01/03).
# XFAIL: *

# Role: verifier — negative lit: AsmParser Bundle.canAdd fail-closed (AIEBaseAsmParser.h:192-201 processMatchedInstruction → Error "incorrect.

# B3.7 negative lit: AsmParser Bundle.canAdd fail-closed
# (AIEBaseAsmParser.h:192-201 processMatchedInstruction → Error "incorrect
# bundle"). Illegal co-issue must not emit a parcel.
#
# Authority is format/slot canAdd — not count-only "bundle exceeds 3".

# Three ADD64: only S1|S2 legal → third saturates and fails canAdd.
# CHECK: error: incorrect bundle

{ add64 d0, d1, d2; add64 d3, d4, d5; add64 d6, d7, d8 }

# Four ADD32: three fill S2/S1/S0; fourth fails canAdd (not a hard count
# gate — supersedes pre-B3.7 "bundle exceeds 3 issue slots").
# CHECK: error: incorrect bundle
{ add32 r0, r1, r2; add32 r3, r4, r5; add32 r6, r7, r8; add32 r9, r10, r11 }
