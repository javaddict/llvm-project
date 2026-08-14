; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -O0 < %s | FileCheck %s --check-prefix=CHECK-O0

; Role: semantic — LOADI32 / constant rematerialisation must use ISA LUI (imm12 → bits[31:20], i.e.

; LOADI32 / constant rematerialisation must use ISA LUI (imm12 →
; bits[31:20], i.e. << 20) via HaydnMatInt — never the pre-ISA-43
; (Val>>16) + LUI + ADDI split.
;
; Critical poison: 0xFFFF under the old expander became
; lui rN, 1; addi32 rN, rN, 65535 → 0x0010FFFF on real LUI
; which then poisoned AND masks and (when remat/spilled) load bases →
; BundleSim MEMORY_FAULT ALIGNMENT on ld32 from 0x10ffff.

define i32 @const_0xffff() {
; CHECK-LABEL: const_0xffff:
; 0xFFFF as pure constant: single addi/andi materialize — must NOT be
; lui 1 + addi 65535 (that yields 0x10FFFF).
; CHECK-NOT: lui{{.*}}, 1
; CHECK: {{addi32|andi32}}{{.*}}65535
;
; CHECK-O0-LABEL: const_0xffff:
; CHECK-O0-NOT: lui{{.*}}, 1
; CHECK-O0: {{addi32|andi32}}{{.*}}65535
  ret i32 65535
}

define i32 @const_0x10000() {
; CHECK-LABEL: const_0x10000:
; CHECK: addi32{{(_w)?}}{{.*}}65536
; CHECK-NOT: lui{{.*}}, 1
;
; CHECK-O0-LABEL: const_0x10000:
; CHECK-O0: addi32{{(_w)?}}{{.*}}65536
  ret i32 65536
}

define i32 @const_0x10ffff() {
; CHECK-LABEL: const_0x10ffff:
; Real 0x10ffff: lui 1 (<<20 → 0x100000) + addi 65535 is *correct*.
; CHECK: lui{{.*}}, 1
; CHECK: addi32{{(_w)?}}{{.*}}65535
;
; CHECK-O0-LABEL: const_0x10ffff:
; CHECK-O0: lui{{.*}}, 1
; CHECK-O0: addi32{{(_w)?}}{{.*}}65535
  ret i32 1114111
}

define i32 @and_mask_u16(i32 %x) {
; CHECK-LABEL: and_mask_u16:
; Mask 0xFFFF: product RI folds to andi32 imm (uimm20), not lui-poison path.
; CHECK: andi32{{.*}}65535
; CHECK-NOT: lui{{.*}}, 1
;
; CHECK-O0-LABEL: and_mask_u16:
; CHECK-O0: andi32{{.*}}65535
; CHECK-O0-NOT: lui{{.*}}, 1
  %a = and i32 %x, 65535
  ret i32 %a
}
