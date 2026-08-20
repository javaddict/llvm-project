# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/Disassembler/HaydnDisassembler.cpp --check-prefix=NO-PEEL
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCFormats.cpp --check-prefix=FILL-SHIFT
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/AsmParser/HaydnAsmParser.cpp --check-prefix=NO-PARSER-PEEL

# RelocLayout ValueShift is the one hwloop scale (word field → dump bytes).
# Member decode applies it only on generated HWLR uimm operands — not on a
# logical whose DecoderMethod already shifted, and not via Imm/2 or Imm/4
# retry. Parser does not peel `_S*` (ABS64 is unsuffixed). Extra NOP pads
# do not pick the three-entry row from count.

# NO-PEEL-NOT: Imm / 2
# NO-PEEL-NOT: Imm / 4
# NO-PEEL: haydnFormatEHwloopImmFieldShift
# NO-PEEL: haydnFindFormatEMemberByOpcode
# FILL-SHIFT: haydnFormatEHwloopImmFieldShift
# FILL-SHIFT: applyHwloopDumpBytesToMemberFields
# NO-PARSER-PEEL: isPrivatePlacementOpcode
# NO-PARSER-PEEL-NOT: peelLogicalOpcodeName
# NO-PARSER-PEEL-NOT: UseE3

.text
set_hwloop_w 0, .Lbody, .Lend, 5
.Lbody:
  { nop; abs64 d0, d1 }
.Lend:
  { nop; nop }

# CHECK-LABEL: <.text>:
# CHECK: set_hwloop{{.*}}0, 12, 24, 5
# CHECK-NOT: 48, 96
# CHECK: abs64
# CHECK-NOT: abs64_s
# CHECK: { nop; nop }
