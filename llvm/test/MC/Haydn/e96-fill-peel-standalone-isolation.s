# REQUIRES: haydn-registered-target
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCCodeEmitter.cpp --check-prefix=EMIT --implicit-check-not=peelLogicalOpcodeName
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCFormats.cpp --check-prefix=FILL --implicit-check-not=peelLogicalOpcodeName
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnAsmPrinter.cpp --check-prefix=PRINT --implicit-check-not=peelLogicalOpcodeName
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s --check-prefix=OBJ
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
# RUN: not llvm-mc -triple=haydn-unknown-elf --defsym=FIELDSLOT=1 %s -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=FIELDSLOT

# Isolation wall: skip-Finalize / compiler extra-op never bag-sorts through
# fillFormatEMemberInstFromRawBundle. Reconstruction is PublicHandAsm only.
# fillFormatEMemberInst never calls FromRawBundle. AsmPrinter inverse-at-entry
# is positional only. Standalone public logicals still place (positional /
# Imm-0 / AR-UA POST / CB). Occupancy is haydnCatalogOccupancyName; leftover
# `_S*` / `_E2_` name peel is deleted. FieldSlot / MemberId / extra-op never
# reconstruct. Peer: AIEBaseMCCodeEmitter.cpp:45-68 serializes typed members
# as-is. AIEBaseAsmPrinter.cpp:166-177 lowers as-is.

# EMIT: haydnCatalogOccupancyName
# EMIT: fillFormatEMemberInstFromCompilerRoot
# EMIT: Always false
# EMIT: fillFormatEMemberInstFromRawBundle
# EMIT: haydnFillFormatEMemberInstPositional
# EMIT: fillFormatEMemberInstPublicHandAsm
# EMIT: Matching public logicals still do not FromRawBundle
# EMIT-NOT: tryMode
# FILL: haydnIsCompilerKeepMapExtraOp
# FILL: Compiler LUI vestigial $rs
# FILL: dest-as-ins members
# FILL: haydnFillFormatEMemberInstPositional
# FILL: FieldSlot, committed MemberId, and compiler extra-op never reconstruct
# FILL: Class-bag reconstruction is deleted
# PRINT: Desc-as-is
# PRINT: refuse E2
# PRINT-NOT: haydnFillFormatEMemberInst(*Mem

.ifdef FIELDSLOT
# Residual FieldSlots are not occupancy. Do not recover ABS64 by suffix.
# FIELDSLOT: error: assembler matched a private placement opcode
abs64_s1 d0, d1
.else

.text
  { add32 r1, r2, r3 }
  { move32 r0, r1 }
  { lui r4, 1 }
  { add32 r5, r0, r1; xor32 r6, r2, r3 }
  { d_lqhwua_post 0, d0, r1 }
  { d_ldw_cb_imm 0, d0, r1, 1 }
  { wfi }

# OBJ-LABEL: <.text>:
# OBJ: {{.*}}0: {{.*}}add32
# OBJ: {{.*}}c: {{.*}}move32
# OBJ: {{.*}}18: {{.*}}lui
# OBJ: {{.*}}24: {{.*}}{
# OBJ: add32
# OBJ: xor32
# OBJ: {{.*}}30: {{.*}}d_lqhwua_post
# OBJ: {{.*}}3c: {{.*}}d_ldw_cb_imm
# OBJ: {{.*}}48: {{.*}}wfi
# OBJ-NOT: <unknown>
# OBJ-NOT: one-parcel placement failed
# OBJ-NOT: sequential E2 singleton split
# OBJ-NOT: abs64_s

# Seven parcels × 12 bytes.
# SEC: Name: .text
# SEC: Size: 84

.endif
