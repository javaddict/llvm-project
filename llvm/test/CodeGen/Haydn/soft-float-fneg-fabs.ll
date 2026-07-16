; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: G_FNEG / G_FABS must legalize on soft-float Haydn.
;
; Bug: HaydnLegalizerInfo listed G_FNEG and G_FABS in the bulk
; `.libcallFor({S32, S64})` rule alongside G_FADD/G_FMUL/... (old lines
; 217-218). The legalizer declared them libcallable, but the upstream
; generic libcall path (LegalizerHelper::libcall in
; llvm/lib/CodeGen/GlobalISel/LegalizerHelper.cpp) has NO case for G_FNEG or
; G_FABS — they hit `default: return UnableToLegalize`. So every soft-float
; kernel that negates or abs'es a float crashed:
; LLVM ERROR: unable to legalize instruction: %1:_(s32) = G_FNEG %0:_
; LLVM ERROR: unable to legalize instruction: %3:_(s32) = G_FABS %2:_
; This blocked all 19 NatureDSP kernels that use XT_ABS_S, and any float
; kernel with `-x` or `__builtin_fabsf`. The soft-float agent worked around
; it in haydn_dsp.h (XT_NEG_S as `0.0f - x`, XT_ABS_S as a volatile-masked
; integer AND) — those workarounds can be removed once this fix lands.
;
; Root cause: there is no __negsf2 libcall (negation is a sign-bit flip), and
; although FABS_F32/FABS_F64 exist in RuntimeLibcalls.td, the generic GISel
; libcall dispatch does not route G_FABS to them. The canonical soft-float
; idiom (matches SelectionDAG ISel for ISD::FNEG/ISD::FABS on RISC-V and
; every other soft-float target) is the integer bit-trick on the IEEE-754
; bit-pattern:
; fneg(x) = bitcast<iN>(x) XOR 0x8000...0; flip sign bit
; fabs(x) = bitcast<iN>(x) AND 0x7FFF...F; clear sign bit
;
; Fix: G_FNEG / G_FABS are now `customFor({S32, S64})` and lowered in
; legalizeCustom via buildXor (sign mask) / buildAnd (magnitude mask) on the
; float-typed destination vreg. Haydn has no FPU, so every float
; value already lives in a GPR/DR64 as its bit-cast integer — the bit-trick
; runs entirely in the integer register file.
;
; Test design: each function negates or abs'es a float/double argument and
; returns it. Before the fix, `llc -global-isel-abort=1` aborted with
; "unable to legalize instruction: G_FNEG/G_FABS". After the fix:
; fneg lowers to XOR32 with 0x80000000 (lui r2, 524288 = 0x80000 << 12 =
; 0x80000000); XOR64 with the 64-bit sign mask.
; fabs lowers to AND32 with 0x7FFFFFFF (lui r2, 524287 = 0x7FFFF << 12 =
; 0x7FFFF000; then addi32 low-12 0xFFF... no — actually 0x7FFFFFFF =
; lui 524287 (0x7FFFF000) + addi32 4095 (0xFFF) -- but the lui+addi32
; materialization may collapse to whatever Haydn's constant pool emits).
;
; The CHECKs assert the XOR/AND opcode appears in the output. If the fix
; regresses, llc crashes before emitting any code and every CHECK fails.
; `-verify-machineinstrs` is on the RUN line so a malformed lowering (wrong
; register class, missing constraints) is caught here, not at M2/M4.
;
; Reference: -softfloat-gfneg-gfabs-integer-bit-trick.md
; gfneg-gfabs-libcallfor-declared-but-not-legalizing.md
; LLVM LegalizerHelper::libcall default case (no G_FNEG/G_FABS).

;float fneg — XOR32 with 0x80000000 (sign-bit flip).
define float @negf(float noundef %a) {
; CHECK-LABEL: negf:
; CHECK:       xor32
; CHECK-NOT:   unable to legalize
  %r = fneg float %a
  ret float %r
}

;float fabs — AND32 with 0x7FFFFFFF (clear sign bit).
define float @fabsf_test(float noundef %a) {
; CHECK-LABEL: fabsf_test:
; CHECK:       and32
; CHECK-NOT:   unable to legalize
  %r = call float @llvm.fabs.f32(float %a)
  ret float %r
}

declare float @llvm.fabs.f32(float)

;double fneg — XOR64 with 0x8000000000000000 (sign-bit flip on DR64).
; The 64-bit XOR lowers to XOR64 in the DR64 file.
define double @negd(double noundef %a) {
; CHECK-LABEL: negd:
; CHECK:       xor64
; CHECK-NOT:   unable to legalize
  %r = fneg double %a
  ret double %r
}

;double fabs — AND64 with 0x7FFFFFFFFFFFFFFF (clear sign bit on DR64).
define double @fabsd_test(double noundef %a) {
; CHECK-LABEL: fabsd_test:
; CHECK:       and64
; CHECK-NOT:   unable to legalize
  %r = call double @llvm.fabs.f64(double %a)
  ret double %r
}

declare double @llvm.fabs.f64(double)

;fneg feeding fadd — exercises the lowered fneg feeding the soft-float
; libcall path (__addsf3), proving the integer bit-trick result flows
; cleanly into a real float op without verifier breakage.
define float @neg_then_add(float noundef %a, float noundef %b) {
; CHECK-LABEL: neg_then_add:
; CHECK:       xor32
; CHECK:       jal_w{{(\.s[012])?}} lr, __addsf3
  %na = fneg float %a
  %r = fadd float %na, %b
  ret float %r
}
