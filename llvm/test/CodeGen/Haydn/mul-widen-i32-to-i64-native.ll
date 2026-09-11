; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — 32x32->64 widening multiply must lower to a native MUL64 widening op, NOT to a JAL __muldi3/__mulsi3 libcall.

; REGRESSION TEST: 32x32->64 widening multiply must lower to a native MUL64
; widening op, NOT to a JAL __muldi3/__mulsi3 libcall.
;
; Bug #19: G_MUL <s64> from `(int64_t)(int32_t)a * (int32_t)b` was lowered to
; LIBCALL_MUL64 -> JAL __muldi3, but no runtime stub exists for __muldi3
; (llvm-libc / compiler-rt provides the 8 division stubs but no multiply
; stub). The jal_w target therefore resolved to ELF symbol index 0 (null) and
; every FIR/IIR/Q31 kernel using the C widening multiply crashed at runtime
; (jal_w -> 0x0), plus the call cost 30-50 cycles. Fix : in
; HaydnLegalizerInfo::legalizeCustom, when G_MUL <s64> operands both trace to
; a G_SEXT/G_ZEXT/G_ANYEXT of an s32 value, emit a native MUL64 widening op
; directly into DR64.
;
; fix: the widening op is selected by operand extension kind
; sext x sext -> MUL64_LL (signed x signed), zext/anyext x zext/anyext ->
; MUL64_ULUL (unsigned x unsigned). The low 32 bits are sign-agnostic but the
; high 32 bits differ, and the unsigned widening case MUST use MUL64_ULUL or
; the high-32 bits corrupt whenever bit 31 of a widened operand is set.
; (MUL64_ULL is unsigned x SIGNED per the ISA -- rs2 sign-extended -- and is
; NOT correct for unsigned x unsigned; it would corrupt the high half.)
;
; Test design: each variant widens two i32 args and multiplies. The full
; 64-bit product is returned so the multiply is not DCE'd. If the
; LIBCALL_MUL64 regression returns, the CHECK-NOT lines trip.

; Widening signed 32x32->64 multiply -- the primary bug-#19 shape.
; Operands are i32 -> sext to i64 -> mul i64. The signed widening multiply's
; 64-bit result equals the signed x signed 32x32 product, so MUL64_LL.

define i64 @widen_mul_sext_i32_i64(i32 %a, i32 %b) {
; CHECK-LABEL: widen_mul_sext_i32_i64:
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__muldi3
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__mulsi3
; CHECK: mul64.ll{{.*}}
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__muldi3
  %aa = sext i32 %a to i64
  %bb = sext i32 %b to i64
  %m = mul i64 %aa, %bb
  ret i64 %m
}

; Widening via zero-extend (e.g. `(uint64_t)(uint32_t)a * b`). fix: the
; unsigned widening multiply MUST use MUL64_ULUL (unsigned x unsigned), not
; MUL64_LL (signed). MUL64_LL sign-extends the wrong half when bit 31 is set
; and corrupts the high 32 bits of the product. The low 32 bits are correct
; under either opcode, so this regression is invisible to a low-32-only
; consumer. (MUL64_ULL is unsigned x SIGNED per the ISA and is NOT correct
; here; only ULUL is unsigned x unsigned.)
define i64 @widen_mul_zext_i32_i64(i32 %a, i32 %b) {
; CHECK-LABEL: widen_mul_zext_i32_i64:
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__muldi3
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__mulsi3
; CHECK: mul64.ulul{{.*}}
  %aa = zext i32 %a to i64
  %bb = zext i32 %b to i64
  %m = mul i64 %aa, %bb
  ret i64 %m
}

; Mixed sign: one sext, one zext. The widening-multiply gate does not match
; (neither BothSigned nor BothUnsigned), so this falls through to the
; schoolbook path, which zero-extends the halves and lowers each partial to
; MUL64_ULUL (unsigned x unsigned). The mod-2^64 result is correct under any
; extension kind when all partials are unsigned.
define i64 @widen_mul_mixed_ext_i32_i64(i32 %a, i32 %b) {
; CHECK-LABEL: widen_mul_mixed_ext_i32_i64:
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__muldi3
; CHECK: mul64.ulul{{.*}}
  %aa = sext i32 %a to i64
  %bb = zext i32 %b to i64
  %m = mul i64 %aa, %bb
  ret i64 %m
}

; Accumulator form: `acc += (int64_t)a * b`. PreLegalizer fuses to G_MULA64
; -> MULA64_LL (formMACs is FATED). Standalone MUL64_LL is also acceptable
; if fusion misses. Either way it must NOT be a libcall.
define i64 @widen_mul_acc_i32_i64(i32 %a, i32 %b, i64 %acc) {
; CHECK-LABEL: widen_mul_acc_i32_i64:
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__muldi3
; CHECK-DAG: mul{{64\.ll|a64\.ll}}
  %aa = sext i32 %a to i64
  %bb = sext i32 %b to i64
  %m = mul i64 %aa, %bb
  %r = add i64 %m, %acc
  ret i64 %r
}

; Unsigned accumulator form: `acc += (uint64_t)(uint32_t)a * b` fuses to
; MULA64_ULUL (Wave T5.1). MULA64_ULL is u×s and must NOT be selected.
define i64 @widen_mul_acc_zext_i32_i64(i32 %a, i32 %b, i64 %acc) {
; CHECK-LABEL: widen_mul_acc_zext_i32_i64:
; CHECK-NOT: {{lui|addi32|jal}}{{.*}}__muldi3
; CHECK: mula64.ulul
  %aa = zext i32 %a to i64
  %bb = zext i32 %b to i64
  %m = mul i64 %aa, %bb
  %r = add i64 %acc, %m
  ret i64 %r
}
