# REQUIRES: haydn-registered-target
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCCodeEmitter.cpp --check-prefix=EMIT --implicit-check-not=peelLogicalOpcodeName
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s --check-prefix=OBJ
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
# RUN: not llvm-mc -triple=haydn-unknown-elf --defsym=FIELDSLOT=1 %s -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=FIELDSLOT

# Isolation wall: skip-Finalize / compiler extra-op never bag-sorts through
# fillFormatEMemberInstFromRawBundle. Standalone public logicals still place
# (positional / Imm-0 / AR-UA POST / CB). Occupancy is
# haydnCatalogOccupancyName; leftover `_S*` / `_E2_` name peel is deleted.
# Peer: AIEBaseMCCodeEmitter.cpp:45-68 serializes typed members as-is.

# EMIT: haydnCatalogOccupancyName
# EMIT: fillFormatEMemberInstFromCompilerRoot
# EMIT: fillFormatEMemberInstFromRawBundle
# EMIT: Compiler LUI vestigial $rs
# EMIT: dest-as-ins members
# EMIT: Always false
# EMIT-NOT: tryMode

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
  { d_lqhwua_post d0, 0, r1, r2, 0 }
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
