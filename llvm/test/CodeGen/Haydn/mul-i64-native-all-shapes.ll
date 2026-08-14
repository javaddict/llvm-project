; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s \
; RUN:   --implicit-check-not=__muldi3 --implicit-check-not=__mulsi3

; Role: verifier — (G7): every shape of 64-bit multiply must lower to native MUL64_LL partial products, NEVER to a __muldi3 / __mulsi3 libcall.

; REGRESSION TEST (G7): every shape of 64-bit multiply must lower to native
; MUL64_LL partial products, NEVER to a __muldi3 / __mulsi3 libcall.
;
; Bug: / wired the widening case ((int64_t)(int32_t)a * b) to native
; MUL64_LL, but five other shapes of G_MUL <s64> still fell through to the
; LIBCALL_MUL64 -> __muldi3 path:
; 1. signed full 64x64 (i64 arg * i64 arg)
; 2. unsigned full 64x64 (u64 arg * u64 arg)
; 3. constant operand (i64 arg * i64 constant)
; 4. mul-then-truncate ((i64*i64) truncated to i32)
; 5. mixed full + ext (i64 arg * (int64_t)(int32_t)b)
; A vec/complex/math/matop audit found 47/201 kernels still emitting __muldi3
; (23%). Haydn has no __muldi3 runtime stub (only the 8 division stubs in
; llvm-libc / compiler-rt), so the jal_w resolved to ELF symbol index 0
; (null) and crashed at runtime -- a correctness defect on top of the
; 30-50 cycle call cost.
;
; Fix : HaydnLegalizerInfo::legalizeCustom now lowers the true-64x64 case
; to a native schoolbook sequence: split each operand into 32-bit halves, form
; three partial products (LL = aLo*bLo, LH = aLo*bHi, HL = aHi*bLo), then
; result = LL + ((LH + HL) << 32) mod 2^64. The HH partial (aHi*bHi)
; contributes only to bits >= 64 and is dropped.
;
; Signedness of the partials (fix): MUL64_LL is signed x signed
; MUL64_ULUL is unsigned x unsigned, and MUL64_ULL is unsigned x SIGNED (rs2
; sign-extended) per the ISA (spec slot1_mac_opcode_table.md:59,66). G_MUL
; <s64> is signless at the IR level, but only UNSIGNED 32x32->64 widening
; produces the correct high-32 bits of each partial when bit 31 of a partial
; operand is set, so the schoolbook path (shapes 1-5) uses MUL64_ULUL. The
; widening 32x32->64 case selects per operand extension: sext x sext ->
; MUL64_LL (the signed widening multiply is exactly the signed x signed
; product), zext x zext -> MUL64_ULUL (shape 7). (Earlier code used MUL64_ULL
; for the unsigned cases, but MUL64_ULL is unsigned x SIGNED and also corrupts
; the high half for unsigned x unsigned -- only MUL64_ULUL is correct.) Together
; these cover every G_MUL <s64> shape -- no libcall remains.
;
; Test design: each function exercises one shape. The --implicit-check-not on
; the RUN line asserts that __muldi3 / __mulsi3 appear NOWHERE in the output.
; Each function also has explicit CHECK-COUNT for the native partials so the
; test fails if the multiply vanishes entirely (e.g. DCE'd or folded to a
; non-multiply), and CHECK-NOT for the wrong opcode variant. If the
; LIBCALL_MUL64 regression returns for any shape, the implicit-check-not trips
; and names the offending function. (Per-bundle packetization/scheduling may
; drift; CHECK-COUNT on the mnemonic occurrence is robust to it.)

;Shape 1: signed full 64x64 (both operands are full i64 args).
; Schoolbook path: three MUL64_ULUL (unsigned x unsigned) partials (fix).

define i64 @mul_s64_full(i64 %a, i64 %b) {
; CHECK-LABEL: mul_s64_full:
; CHECK-COUNT-3: mul64.ulul
; CHECK-NOT: __muldi3
; CHECK: jalr{{(\.s[012])?}}
  %r = mul i64 %a, %b
  ret i64 %r
}

;Shape 2: unsigned full 64x64 (both operands are full u64 args).
; Schoolbook path: three MUL64_ULUL (unsigned x unsigned) partials. The
; partials MUST be unsigned -- a signed MUL64_LL sign-extends each operand's
; wrong half when bit 31 of a low-32 partial operand is set, corrupting the
; high 32 bits of the result. This is exactly (MurmurHash3 finalizer on
; uint64_t). (MUL64_ULL is unsigned x SIGNED per the ISA and would also
; corrupt the high half; only MUL64_ULUL is unsigned x unsigned.)
define i64 @mul_u64_full(i64 %a, i64 %b) {
; CHECK-LABEL: mul_u64_full:
; CHECK-COUNT-3: mul64.ulul
; CHECK-NOT: __muldi3
; CHECK: jalr{{(\.s[012])?}}
  %r = mul i64 %a, %b
  ret i64 %r
}

;Shape 3: constant operand (i64 arg * i64 constant).
; Schoolbook path: three MUL64_ULUL partials.
define i64 @mul_s64_const(i64 %a) {
; CHECK-LABEL: mul_s64_const:
; CHECK-COUNT-3: mul64.ulul
; CHECK-NOT: __muldi3
; CHECK: jalr{{(\.s[012])?}}
  %r = mul i64 %a, 4294967297 ; 0x100000001
  ret i64 %r
}

;Shape 4: mul-then-truncate ((i64*i64) result only low 32 bits used).
; Schoolbook path: three MUL64_ULUL partials. The trunc is selected after.
define i32 @mul_s64_trunc(i64 %a, i64 %b) {
; CHECK-LABEL: mul_s64_trunc:
; CHECK-COUNT-3: mul64.ulul
; CHECK-NOT: __muldi3
; CHECK: jalr{{(\.s[012])?}}
  %m = mul i64 %a, %b
  %r = trunc i64 %m to i32
  ret i32 %r
}

;Shape 5: mixed full + extended (full i64 arg * sign-extended i32).
; Only one operand traces to a widened s32 -> schoolbook path -> MUL64_ULUL.
define i64 @mul_mixed_full_ext(i64 %a, i32 %b) {
; CHECK-LABEL: mul_mixed_full_ext:
; CHECK-COUNT-3: mul64.ulul
; CHECK-NOT: __muldi3
; CHECK: jalr{{(\.s[012])?}}
  %bb = sext i32 %b to i64
  %r = mul i64 %a, %bb
  ret i64 %r
}

;Shape 6 (control): both operands sign-extended from i32 -- the widening
; 32x32->64 SIGNED case. Lowers to a single MUL64_LL (signed x signed).
define i64 @mul_sext_i32_widen(i32 %a, i32 %b) {
; CHECK-LABEL: mul_sext_i32_widen:
; CHECK-COUNT-1: mul64.ll
; CHECK-NOT: mul64.ulul
; CHECK-NOT: __muldi3
; CHECK: jalr{{(\.s[012])?}}
  %aa = sext i32 %a to i64
  %bb = sext i32 %b to i64
  %r = mul i64 %aa, %bb
  ret i64 %r
}

;Shape 7 (control, fix): both operands zero-extended from i32
; the widening 32x32->64 UNSIGNED case. Lowers to a single MUL64_ULUL
; (unsigned x unsigned). The previous code used MUL64_LL here, which
; sign-extended each operand's wrong half when bit 31 was set and corrupted
; the high 32 bits of the product -- 's two-round hash failure. (An
; earlier fix attempted MUL64_ULL, but MUL64_ULL is unsigned x SIGNED per the
; ISA and also corrupts the high half; the correct unsigned x unsigned opcode
; is MUL64_ULUL.)
define i64 @mul_zext_i32_widen(i32 %a, i32 %b) {
; CHECK-LABEL: mul_zext_i32_widen:
; CHECK-COUNT-1: mul64.ulul
; CHECK-NOT: mul64.ll.
; CHECK-NOT: __muldi3
; CHECK: jalr{{(\.s[012])?}}
  %aa = zext i32 %a to i64
  %bb = zext i32 %b to i64
  %r = mul i64 %aa, %bb
  ret i64 %r
}
