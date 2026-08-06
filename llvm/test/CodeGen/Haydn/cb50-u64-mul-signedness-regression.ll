; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s \
; RUN:   | FileCheck %s --implicit-check-not=__muldi3

; Role: verifier — 64-bit unsigned multiply must lower to MUL64_ULUL (unsigned x unsigned) partial products, NOT MUL64_LL (signed x signed) and.

; REGRESSION TEST : 64-bit unsigned multiply must lower to MUL64_ULUL
; (unsigned x unsigned) partial products, NOT MUL64_LL (signed x signed) and
; NOT MUL64_ULL (unsigned x SIGNED -- rs2 sign-extended per the ISA).
;
; Bug: HaydnLegalizerInfo::lowerMul64Schoolbook sign-extended all four 32-bit
; halves of the two operands and lowered each 32x32->64 partial to MUL64_LL.
; The legalizer's IsWidenedFromS32 gate then matched sext-of-s32 -> MUL64_LL
; for every partial. MUL64_LL is signed x signed (spec
; slot1_mac_opcode_table.md:59): when bit 31 of a partial operand is set
; MUL64_LL sign-extends the WRONG half and produces the wrong high-32 bits of
; the 64-bit product. The low-32 bits are sign-agnostic and were correct, so
; the bug was masked whenever only the low 32 bits of the result were consumed.
;
; Trigger: MurmurHash3 finalizer `x ^= x>>33; x *= K` on a uint64_t whose
; post-xor value has bit 31 set in a low-32 half. One round happened to pass
; (only the low byte was tested and it matched by coincidence); two rounds
; accumulated the high-32 corruption and failed (sim returned 49, host 21).
;
; Fix: lowerMul64Schoolbook zero-extends (buildZExt) instead of sign-extends
; and the widening-multiply gate in legalizeCustom selects MUL64_ULUL when both
; operands are zext/anyext of s32. (Earlier code selected MUL64_ULL, which is
; unsigned x SIGNED per the ISA -- rs2 sign-extended -- and ALSO corrupts the
; high half for unsigned x unsigned; the correct unsigned x unsigned opcode is
; MUL64_ULUL.) Verified against the BundleSim ISS.
;
; Test design: the IR uses `mul i64` (signless at the IR level -- the low 64
; bits are the same whether the operands are read as signed or unsigned).
; The CHECK pins the schoolbook partial-product opcode to `mul64.ulul` so the
; signed-MUL64_LL (or unsigned-signed MUL64_ULL) regression trips this test if
; it returns. The function is
; `noinline` and returns the high byte of the product so the value is
; load-bearing (the multiply cannot be DCE'd).

define dso_local i64 @cb50_round(i64 %x, i64 %k) noinline {
; CHECK-LABEL: cb50_round:
; The schoolbook path must emit mul64.ulul (unsigned x unsigned), not
; mul64.ll (signed) and not mul64.ull (unsigned x signed).
; AsmPrinter uses underscore; objdump may print a dotted sub-variant.
; CHECK: mul64{{[._]}}ulul
  %r = mul i64 %x, %k
  ret i64 %r
}

; Two-round hash chain -- the original trigger. Each round is
; `x ^= x>>33; x *= K`. The high-32 corruption from signed MUL64_LL partials
; accumulates across rounds, so a single-round test would miss it.
define dso_local i64 @cb50_hash2(i64 %x) noinline {
; CHECK-LABEL: cb50_hash2:
; CHECK: mul64{{[._]}}ulul
  %s1 = lshr i64 %x, 33
  %x1 = xor i64 %s1, %x
  %m1 = mul i64 %x1, -49064778989728563   ; 0xFF51AFD7ED558CCD
  %s2 = lshr i64 %m1, 33
  %x2 = xor i64 %s2, %m1
  %m2 = mul i64 %x2, -4265267296055464877 ; 0xC4CEB9FE1A85EC53
  ret i64 %m2
}
