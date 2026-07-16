; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: SFR-writing instructions must not be eliminated as dead code.
;
; Bug: HaydnInst base class sets hasSideEffects = 0. Instructions that write
; SFR (Status Flag Register) via "let Defs = [SFR]" inherited this default.
; MachineDCE and MachineSink treated them as dead and removed them, since SFR
; is an implicit def not visible in normal def-use chains. However, SFR writes
; have side effects -- they set condition flags used by subsequent branch
; predication (BEQ/BNE/BGE/BGEU/BLT/BLTU) and conditional moves (MOVT32/MOVF32).
;
; Fix: Added hasSideEffects = 1 to all "let Defs = [SFR] in { }" blocks in
; HaydnInstrInfo.td (arithmetic, logical, shift, DSP, compare, branch, ALU64).
;
; Test design: Each function performs an ALU operation whose result is stored
; to a global, so it is "live" and must survive through codegen. The CHECK lines
; verify the instructions are still present.
;
; IMPORTANT: Previous version used void functions with dead results. IR-level DCE
; removed the dead arithmetic before ISel ran, so no instruction was ever emitted.
; The fix is to store results to a global, keeping the computation live.
;
; If these CHECK lines fail because the instructions disappeared, the
; hasSideEffects flag was lost again -- do NOT just update the CHECK lines.

@g_sink = global i32 0

; ADD32 writes SFR. Result is stored to global to keep it live.
define void @test_add_dead(i32 %a, i32 %b) {
; CHECK-LABEL: test_add_dead:
; CHECK: add32
; CHECK: st32
  %result = add i32 %a, %b
  store i32 %result, ptr @g_sink
  ret void
}

; SUB32 writes SFR. Result is stored to global to keep it live.
define void @test_sub_dead(i32 %a, i32 %b) {
; CHECK-LABEL: test_sub_dead:
; CHECK: sub32
; CHECK: st32
  %result = sub i32 %a, %b
  store i32 %result, ptr @g_sink
  ret void
}

; AND32 writes SFR. Result is stored to global to keep it live.
define void @test_and_dead(i32 %a, i32 %b) {
; CHECK-LABEL: test_and_dead:
; CHECK: and32
; CHECK: st32
  %result = and i32 %a, %b
  store i32 %result, ptr @g_sink
  ret void
}

; OR32 writes SFR. Result is stored to global to keep it live.
define void @test_or_dead(i32 %a, i32 %b) {
; CHECK-LABEL: test_or_dead:
; CHECK: or32
; CHECK: st32
  %result = or i32 %a, %b
  store i32 %result, ptr @g_sink
  ret void
}

; s32 multiply lowers to a DR64 mul64.ll sequence (removed the invented
; scalar MUL32). Result is stored to global to keep it live.
define void @test_mul_dead(i32 %a, i32 %b) {
; CHECK-LABEL: test_mul_dead:
; CHECK: mul64.ll
; CHECK: d_sw_l_with_imm
  %result = mul i32 %a, %b
  store i32 %result, ptr @g_sink
  ret void
}
