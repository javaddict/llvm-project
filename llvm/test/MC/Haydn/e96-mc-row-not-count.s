# REQUIRES: haydn-registered-target
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCChecker.cpp --check-prefix=NO-COUNT-CHECKER
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnInstPrinter.cpp --check-prefix=NO-COUNT-PRINTER
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/Disassembler/HaydnDisassembler.cpp --check-prefix=NO-COUNT-DISASM --implicit-check-not=peelLogicalOpcodeName --implicit-check-not=lookupLogicalOpcode
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCCodeEmitter.cpp --check-prefix=NO-COUNT-EMIT --implicit-check-not=tryMode
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCFormats.cpp --check-prefix=NO-COUNT-FILL
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s --check-prefix=OBJ
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=E3ONLY=1 %s -o %te3.o
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %te3.o | \
# RUN:   FileCheck %s --check-prefix=E3
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=IDLE=1 %s -o %tidle.o
# RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %tidle.o | \
# RUN:   FileCheck %s --check-prefix=IDLE

# Standalone/disasm/printer/checker row identity is the stamped Format E
# row (header entry_num / membership / PacketFormats first-covering), never
# child/text cardinality. Extra textual NOP pads on a dual-mode pack still
# encode as the two-entry row. E3-only two-text packs dump the three-entry
# face. Full-bundle idle dumps NOP in every stamped-row slot.

# NO-COUNT-CHECKER-NOT: TextEntries >= 3
# NO-COUNT-CHECKER-NOT: ForceE3
# NO-COUNT-CHECKER: haydnFormatELogicalIsE3Only
# NO-COUNT-CHECKER: haydnFormatELogicalIsE2Only
# NO-COUNT-PRINTER-NOT: Children.size() > 2
# NO-COUNT-PRINTER: BUNDLE_E96_TWO_ENTRY
# NO-COUNT-PRINTER: BUNDLE_E96_THREE_ENTRY
# NO-COUNT-DISASM: HaydnRelocLayout.h
# NO-COUNT-DISASM: findFixupFromFixupFields
# NO-COUNT-DISASM: getRelocFieldInfo
# NO-COUNT-DISASM: BUNDLE_E96_TWO_ENTRY
# NO-COUNT-DISASM: BUNDLE_E96_THREE_ENTRY
# NO-COUNT-DISASM-HDR: HaydnRelocLayout.h
# NO-COUNT-EMIT: haydnSelectStandaloneFormatEOpcode
# NO-COUNT-EMIT-NOT: auto tryMode
# NO-COUNT-EMIT: refuse skip-Finalize
# NO-COUNT-FILL: haydnSelectStandaloneFormatEOpcode
# NO-COUNT-FILL: haydnFillFormatEMemberInst
# NO-COUNT-FILL: Class-bag reconstruction is deleted

.ifdef IDLE
.text
  nop
# IDLE-LABEL: <.text>:
# IDLE: { nop; nop }
# IDLE-NOT: <unknown>
.else
.ifdef E3ONLY
.text
  { sin_cos d0, r1, 1; nop }
# E3-LABEL: <.text>:
# E3: { nop; nop; sin_cos
# E3-NOT: <unknown>
.else
.text
  { add32 r1, r0, r2; nop; nop }
# OBJ-LABEL: <.text>:
# OBJ: { nop; add32 r1, r0, r2 }
# OBJ-NOT: { {{[^}]+}}; {{[^}]+}}; {{[^}]+}} }
# OBJ-NOT: <unknown>
.endif
.endif
