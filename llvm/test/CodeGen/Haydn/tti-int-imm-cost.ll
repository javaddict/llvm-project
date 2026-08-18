; RUN: opt -mtriple=haydn-unknown-elf -S -passes=consthoist < %s | FileCheck %s
;
; REGRESSION TEST: TTI getIntImmCost / getIntImmCostInst (P16a).
;
; Bug: default TTI getIntImmCostInst is TCC_Free, so ConstantHoisting never
; collects integer immediates. Midend hoisting was blind to Haydn MatInt
; cost (ADDI32_W simm20 is 1 instr; else LUI+ADDI32_W / LOADI32 is 2).
;
; Contract: consthoist collects only Cost > TCC_Basic (1).
;   - i32 add/and of a simm20 / uimm20 stays attached (TCC_Free).
;   - i32 add/and/store of a 2-instr MatInt immediate hoists to entry.
;   - GEP immediates stay attached (CodeGenPrepare owns large offsets).
;   - 0 stays attached (R0 soft-zero).
;
; If this regresses to the default TCC_Free hook, expensive immediates
; stop hoisting (the bitcast const materialization CHECKs fail). If
; simm20 is mis-costed as 2, cheap ADDI32_W immediates are wrongly
; hoisted (the CHECK-NOT bitcast lines fail).
;
; Port: RISCVTargetTransformInfo.cpp:120-323; overlay HaydnMatInt.cpp:76-96.

; simm20 max (524287) folds into ADDI32_W. Must not hoist.
define i32 @addi_simm20(i32 %a) {
; CHECK-LABEL: @addi_simm20(
; CHECK-NOT: %const = bitcast
; CHECK: add i32 %a, 524287
; CHECK: add i32 %1, 524287
  %1 = add i32 %a, 524287
  %2 = add i32 %1, 524287
  ret i32 %2
}

; simm20 min. Must not hoist.
define i32 @addi_simm20_min(i32 %a) {
; CHECK-LABEL: @addi_simm20_min(
; CHECK-NOT: %const = bitcast
; CHECK: add i32 %a, -524288
  %1 = add i32 %a, -524288
  %2 = add i32 %1, -524288
  ret i32 %2
}

; 0x12345678 needs LUI+ADDI32_W (HaydnMatInt cost 2). Hoist.
define i32 @addi_matint2(i32 %a) {
; CHECK-LABEL: @addi_matint2(
; CHECK: %const = bitcast i32 305419896 to i32
  %1 = add i32 %a, 305419896
  %2 = add i32 %1, 305419896
  ret i32 %2
}

; uimm20 ANDI32. Must not hoist.
define i32 @andi_uimm20(i32 %a) {
; CHECK-LABEL: @andi_uimm20(
; CHECK-NOT: %const = bitcast
; CHECK: and i32 %a, 255
  %1 = and i32 %a, 255
  %2 = and i32 %1, 255
  ret i32 %2
}

; AND of a 2-instr immediate. Hoist.
define i32 @andi_matint2(i32 %a) {
; CHECK-LABEL: @andi_matint2(
; CHECK: %const = bitcast i32 305419896 to i32
  %1 = and i32 %a, 305419896
  %2 = and i32 %1, 305419896
  ret i32 %2
}

; GEP: never hoist (RISCV TTI.cpp:223-227).
define ptr @gep_nohoist(ptr %p) {
; CHECK-LABEL: @gep_nohoist(
; CHECK-NOT: %const = bitcast
; CHECK: getelementptr i8, ptr %p, i32 305419896
  %1 = getelementptr i8, ptr %p, i32 305419896
  %2 = getelementptr i8, ptr %1, i32 305419896
  ret ptr %2
}

; Store of a 2-instr immediate. Hoist the value.
define void @store_matint2(ptr %p1, ptr %p2) {
; CHECK-LABEL: @store_matint2(
; CHECK: %const = bitcast i32 305419896 to i32
; CHECK: store i32 %const, ptr %p1
  store i32 305419896, ptr %p1, align 4
  store i32 305419896, ptr %p2, align 4
  ret void
}

; 0 is R0 / TCC_Free. Must not hoist.
define void @store_zero(ptr %p1, ptr %p2) {
; CHECK-LABEL: @store_zero(
; CHECK-NOT: %const = bitcast
; CHECK: store i32 0, ptr %p1
  store i32 0, ptr %p1, align 4
  store i32 0, ptr %p2, align 4
  ret void
}

; i64 has no ADDI64. 2-instr-per-half MatInt must hoist.
define i64 @addi64_matint(i64 %a) {
; CHECK-LABEL: @addi64_matint(
; CHECK: %const = bitcast i64 305419896 to i64
  %1 = add i64 %a, 305419896
  %2 = add i64 %1, 305419896
  ret i64 %2
}
