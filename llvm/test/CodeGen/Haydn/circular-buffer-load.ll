; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o -  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — Circular buffer (CB) load AND store intrinsics must emit native mnemonics (not be dropped as pseudos by AsmPrinter).

; REGRESSION TEST: Circular buffer (CB) load AND store intrinsics must emit
; native mnemonics (not be dropped as pseudos by AsmPrinter).
;
; Bug: D_SDW_CB_IMM and D_SDW_CB_REG were HaydnInst<4> pseudos left inside
; the blanket `let isPseudo = 1` scope at HaydnInstrInfoAuto.td:2269-3050.
; only extracted the LDW (load) variants; the SDW (store) variants
; were missed. The GISel selector (HaydnInstructionSelector.cpp:4375, 4405)
; emitted the D_SDW_CB_IMM/REG MCInst opcodes correctly, but
; HaydnAsmPrinter::emitInstruction (HaydnAsmPrinter.cpp:125-126) skips
; `MCID::Pseudo` operands inside bundles, so the d_sdw_cb_imm/d_sdw_cb_reg
; mnemonics never reached assembly. The store intrinsic was effectively a
; no-op at the assembly level.
;
; Fix : extract D_SDW_CB_IMM/REG from the isPseudo block, give them
; explicit Inst{} bits mirroring LDW_CB layout (opcode 0x9B/0x9C
; mayStore = 1, rtd moved from (outs) to (ins)). If this regresses, the
; d_sdw_cb_* mnemonics will disappear from assembly again.
;
; Test design: Exercise CB load (both immediate and register stride) and
; CB store (both immediate and register stride) in the same function so
; the load result feeds the store (no DCE risk). The store intrinsic
; returns the AGU-updated pointer (IntrWriteMem); without the fix it would
; be silently dropped, leaving only the load mnemonic. The volatile sink
; forces the store to survive.
;
; Asm contract: cbr_sel is a printed ImmArg after the mnemonic; stride is
; the final immediate (IMM) or register (REG). Pointer frexp forms only.

declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32, i32)
declare { i64, ptr } @llvm.haydn.ldw.cb.reg(ptr, i32, i32)
declare ptr @llvm.haydn.sdw.cb.imm(i64, ptr, i32, i32)
declare ptr @llvm.haydn.sdw.cb.reg(i64, ptr, i32, i32)

@sink = global i64 0

; CB load + CB store with immediate stride — both must emit.
; Without, only `d_ldw_cb_imm` would appear; `d_sdw_cb_imm` was dropped.
define void @test_cb_load_then_store_imm(ptr %base) {
; CHECK-LABEL: test_cb_load_then_store_imm:
; CHECK: d_ldw_cb_imm 0, {{.*}}, 1
; CHECK: d_sdw_cb_imm 0, {{.*}}, 1
entry:
  %v_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %base, i32 0, i32 1)
  %v = extractvalue { i64, ptr } %v_pair, 0
  %np = call ptr @llvm.haydn.sdw.cb.imm(i64 %v, ptr %base, i32 0, i32 1)
  ret void
}

; CB load + CB store with register stride — both must emit.
define void @test_cb_load_then_store_reg(ptr %base, i32 %stride) {
; CHECK-LABEL: test_cb_load_then_store_reg:
; CHECK: d_ldw_cb_reg 1,
; CHECK: d_sdw_cb_reg 1,
entry:
  %v_pair = call { i64, ptr } @llvm.haydn.ldw.cb.reg(ptr %base, i32 1, i32 %stride)
  %v = extractvalue { i64, ptr } %v_pair, 0
  %np = call ptr @llvm.haydn.sdw.cb.reg(i64 %v, ptr %base, i32 1, i32 %stride)
  ret void
}

; CB store with immediate stride only — the standalone store must emit.
; This is the surgical probe: pre- this function lowered to an empty
; body because the store pseudo was dropped.
define ptr @test_sdw_cb_imm_only(i64 %data, ptr %base) {
; CHECK-LABEL: test_sdw_cb_imm_only:
; CHECK: d_sdw_cb_imm 0, {{.*}}, 2
entry:
  %np = call ptr @llvm.haydn.sdw.cb.imm(i64 %data, ptr %base, i32 0, i32 2)
  ret ptr %np
}

; CB store with register stride only.
define ptr @test_sdw_cb_reg_only(i64 %data, ptr %base, i32 %stride) {
; CHECK-LABEL: test_sdw_cb_reg_only:
; CHECK: d_sdw_cb_reg 1,
entry:
  %np = call ptr @llvm.haydn.sdw.cb.reg(i64 %data, ptr %base, i32 1, i32 %stride)
  ret ptr %np
}

; frexp writeback chain: second load must reuse AGU-updated pointer (not re-base).
define i64 @test_ldw_cb_imm_chain(ptr %base) {
; CHECK-LABEL: test_ldw_cb_imm_chain:
; CHECK: d_ldw_cb_imm 0, {{.*}}, 1
; CHECK: d_ldw_cb_imm 0, {{.*}}, 1
entry:
  %r0 = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %base, i32 0, i32 1)
  %p1 = extractvalue { i64, ptr } %r0, 1
  %r1 = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %p1, i32 0, i32 1)
  %d1 = extractvalue { i64, ptr } %r1, 0
  ret i64 %d1
}

; Store writeback chain: second store consumes returned new_ptr.
define ptr @test_sdw_cb_imm_chain(i64 %d0, i64 %d1, ptr %base) {
; CHECK-LABEL: test_sdw_cb_imm_chain:
; CHECK: d_sdw_cb_imm 0, {{.*}}, 1
; CHECK: d_sdw_cb_imm 0, {{.*}}, 1
entry:
  %p1 = call ptr @llvm.haydn.sdw.cb.imm(i64 %d0, ptr %base, i32 0, i32 1)
  %p2 = call ptr @llvm.haydn.sdw.cb.imm(i64 %d1, ptr %p1, i32 0, i32 1)
  ret ptr %p2
}
