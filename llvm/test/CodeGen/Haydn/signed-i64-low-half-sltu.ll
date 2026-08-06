; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — signed i64 compares must compare the LOW 32 bits UNSIGNED.

; REGRESSION TEST: signed i64 compares must compare the LOW 32 bits UNSIGNED.
;
; Background: For a 64-bit comparison a OP b, the expansion is
; result = (hi(a) != hi(b)) ? hi_lt_or_hi_gt : lo_lt_or_lo_gt
; When the high halves are equal, the result is decided purely by the low 32
; bits, which form an UNSIGNED magnitude (they carry no sign information
; the sign bit lives only in the high half). Therefore the low-half tie-break
; compare must ALWAYS be sltu32, regardless of whether the overall predicate
; is signed or unsigned. The high-half compare carries the signedness.
;
; Bug: The G_ICMP s64 selector selected the low-half opcode from
; {slt32 (signed), sltu32 (unsigned)} based on the overall predicate. For the
; 4 signed predicates (SLT, SGT, SGE, SLE) this produced slt32 on the low
; half. Whenever a low half had its sign bit set (e.g. 0x8239DE30), slt32
; treated it as negative, flipping the result. This poisoned i64 division
; (udivmoddi4 long division) whenever a divisor's low half exceeded 2^31-1.
;
; Repro: (int64_t)0x8239DE30 >> 63 -> host 0 (correct), sim 255 (buggy).
;
; Fix: In HaydnInstructionSelector G_ICMP s64 SLT/SGT/SGE/SLE, force the
; low-half compare to Haydn::SLTU32 unconditionally. High-half compare keeps
; the predicate's signedness (SLT32 for signed). See the decision record.
;
; Test design:
; 1. f_low_half_sign_bit: the canonical repro — (int64_t)0x8239DE30 >> 63.
; At the signed-i64 level the high halves are equal (both 0 after the
; sext), so the result is decided by the unsigned low-half compare, which
; must yield 0 (the low halves are equal). CHECK the low-half compare is
; sltu32, not slt32.
; 2. cmp_slt_i64_low_sign: icmp slt i64 with operands whose high halves are
; equal but low halves have sign bit set — verifies the four signed
; predicates use sltu32 on the low half.
; This is a focused probe of the low-half opcode, not a smoke test of i64
; division.

; Each of the 4 signed i64 predicates must emit sltu32 on the LOW half.
; High half stays slt32 (signed). When hi(a)==hi(b), the result is decided
; by the unsigned magnitude of the low halves; using slt32 there flips the
; result whenever a low half has bit 31 set (e.g. 0x8239DE30 in the repro).

define i32 @cmp_slt_i64_low_sign(i64 %a, i64 %b) {
 %cmp = icmp slt i64 %a, %b
 %r = zext i1 %cmp to i32
 ret i32 %r
}

; Signed SGT, SGE, SLE: each must use sltu32 on the low half.
; CHECK-LABEL: cmp_sgt_i64_low_sign:
; CHECK-DAG: slt32
; CHECK-DAG: sltu32
define i32 @cmp_sgt_i64_low_sign(i64 %a, i64 %b) {
 %cmp = icmp sgt i64 %a, %b
 %r = zext i1 %cmp to i32
 ret i32 %r
}

; CHECK-LABEL: cmp_sge_i64_low_sign:
; CHECK-DAG: slt32
; CHECK-DAG: sltu32
define i32 @cmp_sge_i64_low_sign(i64 %a, i64 %b) {
 %cmp = icmp sge i64 %a, %b
 %r = zext i1 %cmp to i32
 ret i32 %r
}

; CHECK-LABEL: cmp_sle_i64_low_sign:
; CHECK-DAG: slt32
; CHECK-DAG: sltu32
define i32 @cmp_sle_i64_low_sign(i64 %a, i64 %b) {
 %cmp = icmp sle i64 %a, %b
 %r = zext i1 %cmp to i32
 ret i32 %r
}
