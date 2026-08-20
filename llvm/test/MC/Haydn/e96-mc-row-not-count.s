# REQUIRES: haydn-registered-target
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCChecker.cpp --check-prefix=NO-COUNT-CHECKER --implicit-check-not=peelLogicalOpcodeName
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnInstPrinter.cpp --check-prefix=NO-COUNT-PRINTER
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/Disassembler/HaydnDisassembler.cpp --check-prefix=NO-COUNT-DISASM --implicit-check-not=peelLogicalOpcodeName --implicit-check-not=lookupLogicalOpcode
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCCodeEmitter.cpp --check-prefix=NO-COUNT-EMIT --implicit-check-not=tryMode
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCFormats.cpp --check-prefix=NO-COUNT-FILL --implicit-check-not=peelLogicalOpcodeName
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnFormatERecords.h --check-prefix=NO-COUNT-RECORDS
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnBundleMaterialize.h --check-prefix=NO-COUNT-OPC --implicit-check-not=selectProductRowForMemberCount
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnBundlePlan.h --check-prefix=NO-COUNT-PLAN
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/AsmParser/HaydnAsmParser.cpp --check-prefix=NO-COUNT-PARSER --implicit-check-not=selectProductRowForMemberCount
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
# NO-COUNT-CHECKER-NOT: TextEntries
# NO-COUNT-CHECKER-NOT: memberCount > ISSUE
# NO-COUNT-CHECKER-NOT: E2EntryCapacity
# NO-COUNT-CHECKER-NOT: E3EntryCapacity
# NO-COUNT-CHECKER: RowEntryCount
# NO-COUNT-CHECKER: occupancy exceeds selected Format E row
# NO-COUNT-CHECKER: haydnCatalogOccupancyName
# NO-COUNT-CHECKER: haydnFormatELogicalIsE3Only
# NO-COUNT-CHECKER: haydnFormatELogicalIsE2Only
# NO-COUNT-PRINTER-NOT: Children.size() > 2
# NO-COUNT-PRINTER-NOT: SlotN = 2
# NO-COUNT-PRINTER-NOT: SlotN = 3
# NO-COUNT-PRINTER-NOT: SlotN {{\?}} SlotN : Children.size()
# NO-COUNT-PRINTER: getBundleFormatRow
# NO-COUNT-PRINTER: BUNDLE_E96_TWO_ENTRY
# NO-COUNT-PRINTER: BUNDLE_E96_THREE_ENTRY
# NO-COUNT-PRINTER: EntryCount
# NO-COUNT-DISASM: BUNDLE_E96_TWO_ENTRY
# NO-COUNT-DISASM: BUNDLE_E96_THREE_ENTRY
# NO-COUNT-DISASM: haydnFormatEHwloopImmFieldShift
# NO-COUNT-DISASM: haydnFindFormatEMemberByOpcode
# NO-COUNT-DISASM-NOT: Size / 2
# NO-COUNT-DISASM-NOT: Imm / 2
# NO-COUNT-PLAN: haydnSelectStandaloneFormatEOpcode
# NO-COUNT-PLAN: return selectProductRow(ProductFormatMask, MemberCount)
# NO-COUNT-EMIT: haydnSelectStandaloneFormatEOpcode
# NO-COUNT-EMIT-NOT: auto tryMode
# NO-COUNT-EMIT: refuse skip-Finalize
# NO-COUNT-FILL: haydnSelectStandaloneFormatEOpcode
# NO-COUNT-FILL: assignFormatEMemberEntries
# NO-COUNT-FILL: haydnFormatERowForCompositeOpcode
# NO-COUNT-FILL: haydnCatalogOccupancyName
# NO-COUNT-FILL-NOT: N <= Fam.E2EntryCapacity
# NO-COUNT-FILL-NOT: N <= Fam.E3EntryCapacity
# Leftover FieldSlot `*_S<digits>` is not occupancy recovery. Assignment
# capacity is the family handle, not a hardcoded TWO vs THREE map.
# NO-COUNT-RECORDS: E2EntryCapacity
# NO-COUNT-RECORDS: E3EntryCapacity
# NO-COUNT-RECORDS: getDefaultFamilyRecords
# NO-COUNT-RECORDS-NOT: {"_S0"
# NO-COUNT-RECORDS-NOT: "_LD_S0"
# NO-COUNT-FILL: haydnFormatEHwloopImmFieldShift
# NO-COUNT-FILL: findFixupFromFixupFields
# NO-COUNT-FILL: haydnFillFormatEMemberInstPositional
# NO-COUNT-FILL: FieldSlot, committed MemberId, and compiler extra-op never reconstruct
# NO-COUNT-FILL: haydnFillFormatEMemberInst
# NO-COUNT-FILL: Class-bag reconstruction is deleted
# Opcode-list row select: InstSlot, Mode-only membership, then unit cover.
# Extra NOP pads are idle fill. A cover miss keeps ProductDefaultRowID.
# NO-COUNT-OPC: haydnFormatELogicalIsE3Only
# NO-COUNT-OPC: haydnFormatELogicalIsE2Only
# NO-COUNT-OPC: opcodesHaveFormatEUnitCoverForMode
# NO-COUNT-OPC: ProductDefaultRowID
# NO-COUNT-OPC-NOT: Opcodes.size() >= 3
# NO-COUNT-OPC-NOT: Opcodes.size() <= 1
# Parser capacity is a bound on the membership row, never TWO vs THREE
# from child count.
# NO-COUNT-PARSER: selectParsedFormatEComposite
# NO-COUNT-PARSER: haydnFormatERowForCompositeOpcode
# NO-COUNT-PARSER: Selected row capacity is a bound
# NO-COUNT-PARSER-NOT: UseE3
# NO-COUNT-PARSER-NOT: Fam.E2EntryCapacity
# NO-COUNT-PARSER-NOT: Fam.E3EntryCapacity

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
