# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCCodeEmitter.cpp --check-prefix=ISOLATE --implicit-check-not=tryMode
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s --check-prefix=OBJ
# RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
# REQUIRES: haydn-registered-target

# Isolation wall: committed MemberId BUNDLE_E96_* serialize as-is
# (trySerializeFormatECompositeAsIs) and never enter standalone DFS/fill.
# encodeSlotSubInst is serialize-only. fillFormatEMemberInst is hand-asm
# public-logical only (positional / closed keep-map). Compiler extra-op
# cutover (MOVE32 3-op vs member 2-op) stays in Finalize. Suffix _S* does
# not certify a Format E entry. Peer: AIEBaseMCCodeEmitter.cpp:45-68.
#
# ISOLATE: AnyPrivate
# ISOLATE: AnyPublicReal
# ISOLATE: trySerializeFormatECompositeAsIs
# ISOLATE: refuse skip-Finalize
# ISOLATE: encodeSlotSubInst never calls this
# ISOLATE: Operand count is occupancy, not row width
# ISOLATE: never enter this function
# ISOLATE: committed MemberId opcodes return false
# ISOLATE: MOVE32/ABS32 trailing extra

.text
  { add32 r1, r2, r3 }
  { move32 r0, r1 }
  { add32 r4, r0, r1; xor32 r5, r2, r3 }
  { d_lqhwua_post d0, 0, r1, r2, 0 }

# OBJ-LABEL: <.text>:
# OBJ: {{.*}}0: {{.*}}add32
# OBJ: {{.*}}c: {{.*}}move32
# OBJ: {{.*}}18: {{.*}}{
# OBJ: add32
# OBJ: xor32
# OBJ: {{.*}}24: {{.*}}d_lqhwua_post
# OBJ-NOT: <unknown>
# OBJ-NOT: one-parcel placement failed
# OBJ-NOT: sequential E2 singleton split
# OBJ-NOT: refuse skip-Finalize

# Four parcels × 12 bytes.
# SEC: Name: .text
# SEC: Size: 48
