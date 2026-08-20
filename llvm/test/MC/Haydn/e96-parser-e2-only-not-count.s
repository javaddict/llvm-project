# REQUIRES: haydn-registered-target
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCFormats.cpp --check-prefix=NO-PEEL
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/AsmParser/HaydnAsmParser.cpp --check-prefix=NO-PARSER-PEEL
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnInstPrinter.cpp --check-prefix=NO-PRINT-PEEL
# RUN: not llvm-mc -triple=haydn-unknown-elf --defsym=TRIPLE_REAL=1 %s -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=TRIPLE-REAL
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=THREE=1 %s -o %t3.o
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t2.o
# RUN: llvm-objcopy -O binary -j .text %t3.o %t3.bin
# RUN: llvm-objcopy -O binary -j .text %t2.o %t2.bin
# RUN: cmp %t3.bin %t2.bin
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t3.o | \
# RUN:   FileCheck %s --check-prefix=OBJ

# Catalog `{ insn; nop; nop }` must not pick E3 from text count for a
# generated E2-only logical (ADDI32 / SUBI32). Extra NOP fillers are idle
# pad only when both Modes are generated-legal, so the three-text form
# encodes as the two-entry `{ insn; nop }` row. Three *real* members with
# an E2-only logical still fail closed. Residual `_S*` names are refused,
# not peeled, before Mode select.

# NO-PEEL: isResidualFieldSlotName
# NO-PEEL: haydnSelectStandaloneFormatEOpcode
# NO-PEEL: haydnFindFormatEMemberByOpcode
# NO-PEEL-NOT: getLogicalBaseOpcode
# NO-PEEL-NOT: HaydnMemberSlotSuffix
# NO-PARSER-PEEL: isPrivatePlacementOpcode
# NO-PARSER-PEEL-NOT: peelLogicalOpcodeName
# NO-PARSER-PEEL-NOT: getLogicalBaseOpcode
# NO-PARSER-PEEL-NOT: UseE3
# NO-PRINT-PEEL-NOT: peelLogicalOpcodeName
# NO-PRINT-PEEL-NOT: HaydnFormatERecords.h

.ifdef TRIPLE_REAL
# TRIPLE-REAL: error: incorrect bundle: E2-only logical cannot occupy a three-entry row
{ addi32 r1, r0, 1; add32 r2, r0, r0; xor32 r3, r0, r0 }
.else
.ifdef THREE
.text
  { addi32 r1, r0, 1; nop; nop }
  { subi32 r2, r0, 1; nop; nop }
.else
.text
  { addi32 r1, r0, 1; nop }
  { subi32 r2, r0, 1; nop }
.endif
# OBJ-LABEL: <.text>:
# OBJ: {{.*}}0: {{.*}}addi32
# OBJ: {{.*}}subi32
# OBJ-NOT: <unknown>
.endif
