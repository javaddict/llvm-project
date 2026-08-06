# RUN: llvm-mc -triple=haydn-unknown-elf %s | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — EXP2/LOG2/RECIP/SQRT asm→print round-trip (encoders landed).

# Companion to disassembler-dsp-math-roundtrip.s. These LUT fixed-point math
# ops used to be isCodeGenOnly shields (no binary encoder). They now assemble
# and print; this file pins the asm→parse→print contract so a re-shield or
# printer drop cannot silently green. Prefer moving cases into the main
# round-trip lit once objdump encode coverage is added there.

# CHECK: exp2{{.*}}r0, r1
# CHECK: log2{{.*}}r2, r3
# CHECK: recip{{.*}}r4, r5
# CHECK: sqrt{{.*}}r6, r7

exp2	r0, r1
log2	r2, r3
recip	r4, r5
sqrt	r6, r7
