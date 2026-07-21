# RUN: llvm-mc -triple=haydn-unknown-elf %s
#
# Companion to disassembler-dsp-math-roundtrip.s.
#
# These are REAL DB instructions (authoritative spec at
# ~/haydn-plans/Database/haydn_instruction_db.json):
# EXP2 rt, rs (GPR operands, slots {1,2})
# LOG2 rt, rs (GPR operands, slots {1,2})
# RECIP rt, rs (GPR operands, slots {1,2})
# SQRT rt, rs (GPR operands, slots {1,2})
#
# They have NO binary encoder yet (M5 encoding work is in flight). To keep the
# MC emitter from crashing with "LLVM ERROR: Unsupported instruction", these
# four are shielded from the assembler via isCodeGenOnly=1, so llvm-mc rejects
# them with a clean "invalid instruction mnemonic" error instead of crashing.
# The shield is codex-endorsed for crash-prevention.
#
# Because the shield rejects the mnemonic, this assembly does NOT assemble
# today, so the test is expected to fail. When the M5 encoders land, remove the
# isCodeGenOnly shield on these four, MOVE them back into
# disassembler-dsp-math-roundtrip.s as encodable round-trip cases, and delete
# this companion test (its expected-failure will flip to an unexpected-pass
# which is the signal that the shield is ready to come off).


exp2	r0, r1
log2	r2, r3
recip	r4, r5
sqrt	r6, r7
