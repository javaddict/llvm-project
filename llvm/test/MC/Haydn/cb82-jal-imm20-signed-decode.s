# REQUIRES: haydn-registered-target
# Format E96 cutover residual: FileCheck/idle-pad/reloc geometry still open (GE96-01/03).
# XFAIL: *
// CHECK: {{.*}}0: 07 0e f8 24 ec 0f 00 00 00 00 00 00  	{ 	jal	lr; 	nop }
// CHECK: {{.*}}c: 07 0e f8 9c ff 0f 00 00 00 00 00 00  	{ 	jal	lr; 	nop }
// CHECK: {{.*}}18: 07 0e f8 04 00 00 00 00 00 00 00 00  	{ 	jal	lr; 	nop }
// CHECK: {{.*}}24: 07 0e f8 b4 17 00 00 00 00 00 00 00  	{ 	jal	lr; 	nop }
// CHECK: {{.*}}30: 07 0e f8 00 00 00 00 00 00 00 00 00  	{ 	jal	lr; 	nop }
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s --check-prefix=DECODE
# RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC

# Role: object — (/): Format E JAL_S0 call-target decode + symbolic reloc registration for VLIW bundle syntax.

# REGRESSION TEST (/): Format E JAL_S0 call-target decode +
# symbolic reloc registration for VLIW bundle syntax.
#
# (1) Signed imm20 decode: the slot field stores a signed 20-bit PC-relative
# BYTE offset (no ÷2). Without DecoderMethod on calltarget_s0, negative
# offsets rendered as huge unsigned positives (e.g. -5084 → 1043492).
#
# (2) Bundle symbol registration (companion / direct-ELF hard gate):
# `{ jal lr, main; nop; nop }` embeds the JAL as an MCOperand::isInst
# child of Haydn::BUNDLE. Base MCStreamer::emitInstruction only walks
# top-level isExpr operands, so `main` was never registerSymbol'd →
# R_HAYDN_CallSImm20 against symbol index 0 (ABS) → lld patched S=0 →
# garbage offset / `jal r0, 65535` after FieldLsb clobber. HaydnMCELFStreamer
# recursively visitUsedExpr's bundle children (Hexagon pattern).
#
# (3) CallSImm20 geometry (HaydnRelocLayout): FieldLsb=4, ValueShift=0 so the
# reloc patch writes imm20 at LoWord bits[23:4] without overwriting rt
# (bits[3:0]=lr).

# Negative offset: -5084 (field 0xFEC24). Pre-fix: 1043492.

jal lr, -5084
# DECODE-LABEL: Disassembly of section .text:
# DECODE: jal{{.*}}lr, -5084

# Small negative offset: -100. Pre-fix: 1048476.
jal lr, -100
# DECODE: jal{{.*}}lr, -100

# Small positive offset: +4.
jal lr, 4
# DECODE: jal{{.*}}lr, 4

# Larger positive offset: +6068.
jal lr, 6068
# DECODE: jal{{.*}}lr, 6068

# Bundle form with external symbol (the crt0 / direct-ELF canary path).
{ jal lr, main; nop; nop }
# RELOC: R_HAYDN_CallSImm20 main
