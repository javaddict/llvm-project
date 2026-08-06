# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | \
# RUN:   llvm-objdump -d -r --triple=haydn-unknown-elf - | FileCheck %s

# Role: object — Fixup selection must use TableGen enum values / Format E kinds.

# REGRESSION TEST: Fixup selection must use TableGen enum values, not encoding
# opcodes. Format E product path emits R_HAYDN_WIDE_CallSImm20 at **parcel
# origin** (r_offset = 0 within each 12-byte parcel).

#===----------------------------------------------------------------------===#
# JAL must produce R_HAYDN_WIDE_CallSImm20 (20-bit call fixup)
#===----------------------------------------------------------------------===#

jal r0, external_func
# CHECK: {{.*}}0: 07 0e 08 00 00 00 00 00 00 00 00 00  	{ 	jal	r0, 0; 	nop }
# CHECK: 00000000:  R_HAYDN_WIDE_CallSImm20	external_func

#===----------------------------------------------------------------------===#
# Second JAL — reloc still parcel-origin relative to its own parcel
#===----------------------------------------------------------------------===#

jal r0, another_func
# CHECK: {{.*}}c: 07 0e 08 00 00 00 00 00 00 00 00 00  	{ 	jal	r0, 0; 	nop }
# CHECK: 0000000c:  R_HAYDN_WIDE_CallSImm20	another_func

#===----------------------------------------------------------------------===#
# Conditional branch external — RI12 kind, not Call / not BranchSImm16
#===----------------------------------------------------------------------===#

beq r1, r2, external_func
# CHECK: {{.*}}18: {{.*}}
# CHECK: 00000018:  R_HAYDN_WIDE_BranchSImm12_RI	external_func

beqz r3, another_func
# CHECK: {{.*}}24: {{.*}}
# CHECK: 00000024:  R_HAYDN_WIDE_BranchSImm12	another_func
