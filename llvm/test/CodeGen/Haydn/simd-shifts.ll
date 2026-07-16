; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 %s -o - | FileCheck %s
;
; REGRESSION TEST (F18): DR64 shift-amount operand must NOT use MOVE32.
;
; Bug: HaydnInstructionSelector::selectDR64ShiftGPR32 emitted
; MOVE32 AmtGPR32, AmtDR64
; to truncate the i64 shift amount to i32. MOVE32 requires BOTH operands in
; GPR32 (— DR64 is a separate register bank). AmtDR64 is DR64, so the
; verifier aborted under -global-isel-abort=1 with "operand is not a GPR32
; register". This blocked all SIMD shift intrinsics (X2SLL32/X2SRL32/X2SRA32
; and the v4i16 family) wherever the shift amount arrived in a DR64 register
; (the intrinsic signature is (i64, i64) -> i64).
;
; Fix: use the MOV_DR64_TO_GPR pseudo (DR64 -> GPR32 pair cross-bank transfer)
; and keep only the low lane as the shift amount. The high lane is dead and
; gets DCE'd after RA expansion.
;
; What breaks if the bug reappears: llc crashes with a verifier error before
; any code is emitted — the CHECK lines below never get a chance to match.
;
; NOTE: the RUN-line / XFAIL edits belong to the test-hygiene group (F43);
; this test was failing under -global-isel-abort=1 before the selector fix.
;
; REGRESSION TEST : vector shift-amount G_EXTRACT_VECTOR_ELT path.
; The v2i32/v4i16 shift cases (simd_v2i32_shl etc., where the shift amount is
; itself a vector) hit a DIFFERENT selector branch than the F18 i64 path above:
; they build a G_EXTRACT_VECTOR_ELT to pull element 0 of the vector shift
; amount into a scalar GPR32. That dest vreg was created with
; createVirtualRegister(&GPR32RegClass) — a target-class vreg with NO LLT
; and MachineIRBuilder::buildInstr for a generic opcode asserts
; DstOps[0].getLLTTy is scalar/pointer. Fix: create the dest (and the
; G_CONSTANT index) as generic vregs via createGenericVirtualRegister(LLT)
; and constrainGenericRegister to GPR32. If this regresses, llc aborts in
; InstructionSelect with the MachineIRBuilder.cpp:1415 assertion before any
; code is emitted.
;
; SIMD v2i32 shift instructions (X2SLL32, X2SRL32, X2SRA32).
; Each element of the DR64 shift amount vector shifts the corresponding element.

;===----------------------------------------------------------------------===
; SIMD v2i32 Shift Left (X2SLL32)
;===----------------------------------------------------------------------===

define <2 x i32> @simd_v2i32_shl(<2 x i32> %a, <2 x i32> %amt) nounwind {
; CHECK-LABEL: simd_v2i32_shl:
; CHECK: x2sll32
  %result = shl <2 x i32> %a, %amt
  ret <2 x i32> %result
}

;===----------------------------------------------------------------------===
; SIMD v2i32 Logical Shift Right (X2SRL32)
;===----------------------------------------------------------------------===

define <2 x i32> @simd_v2i32_lshr(<2 x i32> %a, <2 x i32> %amt) nounwind {
; CHECK-LABEL: simd_v2i32_lshr:
; CHECK: x2srl32
  %result = lshr <2 x i32> %a, %amt
  ret <2 x i32> %result
}

;===----------------------------------------------------------------------===
; SIMD v2i32 Arithmetic Shift Right (X2SRA32)
;===----------------------------------------------------------------------===

define <2 x i32> @simd_v2i32_ashr(<2 x i32> %a, <2 x i32> %amt) nounwind {
; CHECK-LABEL: simd_v2i32_ashr:
; CHECK: x2sra32
  %result = ashr <2 x i32> %a, %amt
  ret <2 x i32> %result
}

;===----------------------------------------------------------------------===
; SIMD v2i32 Combined Shift Test
;===----------------------------------------------------------------------===

define <2 x i32> @simd_v2i32_shift_combo(<2 x i32> %a, <2 x i32> %amt1, <2 x i32> %amt2) nounwind {
; CHECK-LABEL: simd_v2i32_shift_combo:
; Post-: shift instructions may execute in any slot order.
; CHECK-DAG: x2sll32
; CHECK-DAG: x2srl32
; CHECK-DAG: x2sra32
  %shl = shl <2 x i32> %a, %amt1
  %lshr = lshr <2 x i32> %a, %amt1
  %ashr = ashr <2 x i32> %a, %amt2
  %result = add <2 x i32> %shl, %lshr
  %result2 = sub <2 x i32> %result, %ashr
  ret <2 x i32> %result2
}
