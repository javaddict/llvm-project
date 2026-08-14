; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
;
; The RI20/RI5 immediate ALU forms had no selection path whatsoever: every
; immediate def (ANDI32_S*, ORI32_S*, XORI32_S*, ADDI32_S*, SUBI32_S* and the
; RI5 shift peers) carries an empty pattern list, the C++ selector has no
; G_ADD/G_AND/G_OR/G_XOR case, and HaydnGISel.td only had register-register
; Pats. So `x & 255` came out as
;
;     addi32 r5, r0, 255      <- constant materialized, alone in a bundle
;     and32    r1, r1, r5
;
; and `andi32` / `ori32` / `slli32` / `srli32` / `srai32` were never emitted at
; all. HaydnGISel.td now folds a fitting G_CONSTANT into the immediate form.
;
; This also required fixing HaydnSImm/HaydnUImm: their ImmLeaf bodies were
; missing `return`, so GlobalISelEmitter produced
; `isInt<16>(Imm); llvm_unreachable("simm16 should have returned")` and any Pat
; using them aborted during selection.
;
; Immediate classes differ per opcode, so the range boundaries are checked too:
;   ADDI32 / SUBI32           simm20 (product RI20; max 524287)
;   ANDI32 / ORI32 / XORI32   uimm20 (zero-extended -- no negative immediate)
;   SLLI32 / SRLI32 / SRAI32  uimm5
;
; Note on CHECK-NOT: every function's prologue/epilogue contains
; `xor32 r0, r0, r0` (soft-zero R0) and `subi32`/`addi32 sp`, so a bare
; CHECK-NOT on `or32` would false-match inside `xor32`. Negative checks below
; are pinned to the register-register operand shape.

; CHECK-LABEL: add_imm:
; CHECK: addi32 r{{[0-9]+}}, r{{[0-9]+}}, 12
; CHECK-NOT: add32 r
define i32 @add_imm(i32 %x) {
  %r = add i32 %x, 12
  ret i32 %r
}

; Negative immediate is fine for ADDI32 (simm20 is signed).
; CHECK-LABEL: add_imm_neg:
; CHECK: addi32 r{{[0-9]+}}, r{{[0-9]+}}, -1
; CHECK-NOT: add32 r
define i32 @add_imm_neg(i32 %x) {
  %r = add i32 %x, -1
  ret i32 %r
}

; sub x, C is canonicalized to add x, -C before selection, so SUBI32 is
; exercised via MIR / residual G_SUB rather than plain IR here. ADDI simm20
; already covers the product sub-imm shape (see add_imm_neg).

; CHECK-LABEL: and_imm:
; CHECK: andi32 r{{[0-9]+}}, r{{[0-9]+}}, 255
; CHECK-NOT: and32 r
define i32 @and_imm(i32 %x) {
  %r = and i32 %x, 255
  ret i32 %r
}

; CHECK-LABEL: and_imm_one:
; CHECK: andi32 r{{[0-9]+}}, r{{[0-9]+}}, 1
; CHECK-NOT: and32 r
define i32 @and_imm_one(i32 %x) {
  %r = and i32 %x, 1
  ret i32 %r
}

; `or32` needs a leading-char guard: the soft-zero `xor32 r0, r0, r0` in every
; prologue contains "or32 r0, r0, r0" as a substring.
; CHECK-LABEL: or_imm:
; CHECK: ori32 r{{[0-9]+}}, r{{[0-9]+}}, 4095
; CHECK-NOT: {{[^x]or32 r}}
define i32 @or_imm(i32 %x) {
  %r = or i32 %x, 4095
  ret i32 %r
}

; CHECK-LABEL: xor_imm:
; CHECK: xori32 r{{[0-9]+}}, r{{[0-9]+}}, 7
define i32 @xor_imm(i32 %x) {
  %r = xor i32 %x, 7
  ret i32 %r
}

; CHECK-LABEL: shl_imm:
; CHECK: slli32 r{{[0-9]+}}, r{{[0-9]+}}, 3
; CHECK-NOT: sll32 r
define i32 @shl_imm(i32 %x) {
  %r = shl i32 %x, 3
  ret i32 %r
}

; CHECK-LABEL: lshr_imm:
; CHECK: srli32 r{{[0-9]+}}, r{{[0-9]+}}, 5
; CHECK-NOT: srl32 r
define i32 @lshr_imm(i32 %x) {
  %r = lshr i32 %x, 5
  ret i32 %r
}

; Shift amount 31 is the largest uimm5.
; CHECK-LABEL: ashr_imm:
; CHECK: srai32 r{{[0-9]+}}, r{{[0-9]+}}, 31
; CHECK-NOT: sra32 r
define i32 @ashr_imm(i32 %x) {
  %r = ashr i32 %x, 31
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; Boundaries -- out-of-range constants must stay on the register-register path.
;===----------------------------------------------------------------------===;

; 524287 is the largest simm20 (product RI20 ADDI32).
; CHECK-LABEL: add_imm_simm20_max:
; CHECK: addi32 r{{[0-9]+}}, r{{[0-9]+}}, 524287
; CHECK-NOT: add32 r
define i32 @add_imm_simm20_max(i32 %x) {
  %r = add i32 %x, 524287
  ret i32 %r
}

; 524288 does not fit simm20: materialize (ORI32_W), then ADD32.
; CHECK-LABEL: add_imm_over_simm20:
; CHECK: ori32{{(_w)?}} r{{[0-9]+}}, r0, 524288
; CHECK: add32 r{{[0-9]+}}, r{{[0-9]+}}, r{{[0-9]+}}
define i32 @add_imm_over_simm20(i32 %x) {
  %r = add i32 %x, 524288
  ret i32 %r
}

; 1048575 is the largest uimm20.
; CHECK-LABEL: and_imm_uimm20_max:
; CHECK: andi32 r{{[0-9]+}}, r{{[0-9]+}}, 1048575
; CHECK-NOT: and32 r
define i32 @and_imm_uimm20_max(i32 %x) {
  %r = and i32 %x, 1048575
  ret i32 %r
}

; 1048576 (2^20) does not fit uimm20 -- LUI pair + AND32, never ANDI32.
; CHECK-LABEL: and_imm_over_uimm20:
; CHECK-NOT: andi32
; CHECK: and32 r{{[0-9]+}}, r{{[0-9]+}}, r{{[0-9]+}}
; CHECK-NOT: andi32
define i32 @and_imm_over_uimm20(i32 %x) {
  %r = and i32 %x, 1048576
  ret i32 %r
}

; ANDI32 zero-extends its immediate, so a negative mask must NOT be folded.
; CHECK-LABEL: and_imm_negative:
; CHECK-NOT: andi32
; CHECK: addi32 r{{[0-9]+}}, r0, -16
; CHECK: and32 r{{[0-9]+}}, r{{[0-9]+}}, r{{[0-9]+}}
; CHECK-NOT: andi32
define i32 @and_imm_negative(i32 %x) {
  %r = and i32 %x, -16
  ret i32 %r
}

; `not` is xor x, -1. -1 fails the uimm20 ImmLeaf, so NOT32 must still win and
; XORI32 must not steal this.
; CHECK-LABEL: not_stays_not32:
; CHECK-NOT: xori32
; CHECK: not32 r{{[0-9]+}}, r{{[0-9]+}}
; CHECK-NOT: xori32
define i32 @not_stays_not32(i32 %x) {
  %r = xor i32 %x, -1
  ret i32 %r
}

; Variable shift amount has no immediate form.
; CHECK-LABEL: shl_reg:
; CHECK-NOT: slli32
; CHECK: sll32 r{{[0-9]+}}, r{{[0-9]+}}, r{{[0-9]+}}
; CHECK-NOT: slli32
define i32 @shl_reg(i32 %x, i32 %y) {
  %r = shl i32 %x, %y
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; The exact shape that regressed in Dhrystone: a small constant feeding an
; ALU op, where the materialization used to sit alone in its own bundle
; (dhry_1.c Proc_6 `& 255`, Proc_4 `& 1`).
;===----------------------------------------------------------------------===;

; CHECK-LABEL: dhry_proc6_shape:
; CHECK-NOT: addi32 r{{[0-9]+}}, r0, 255
; CHECK: andi32 r{{[0-9]+}}, r{{[0-9]+}}, 255
define i32 @dhry_proc6_shape(i32 %v) {
  %m = and i32 %v, 255
  ret i32 %m
}
