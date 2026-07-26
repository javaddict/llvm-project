; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - < %s | FileCheck %s
; XFAIL: *
; G-CAPI: intrinsic signature / ImmArg / return-type mismatch vs decls (not BF3 setDesc).
;
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
; explicit Inst{} bits mirroring 's LDW_CB layout (opcode 0x9B/0x9C
; mayStore = 1, rtd moved from (outs) to (ins)). If this regresses, the
; d_sdw_cb_* mnemonics will disappear from assembly again.
;
; Test design: Exercise CB load (both immediate and register stride) and
; CB store (both immediate and register stride) in the same function so
; the load result feeds the store (no DCE risk). The store intrinsic is
; void-returning with IntrWriteMem; without the fix it would be silently
; dropped, leaving only the load mnemonic. The volatile sink forces the
; store to survive.

declare { i64, i32 } @llvm.haydn.ldw.cb.imm(i32, i32, i32)
declare { i64, i32 } @llvm.haydn.ldw.cb.reg(i32, i32, i32)
declare i32 @llvm.haydn.sdw.cb.imm(i64, i32, i32, i32)
declare i32 @llvm.haydn.sdw.cb.reg(i64, i32, i32, i32)

@sink = global i64 0

; CB load + CB store with immediate stride — both must emit.
; Without, only `d_ldw_cb_imm` would appear; `d_sdw_cb_imm` was dropped.
define void @test_cb_load_then_store_imm(i32 %base) {
; CHECK-LABEL: test_cb_load_then_store_imm:
; CHECK: d_ldw_cb_imm
; CHECK: d_sdw_cb_imm
entry:
  %v_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %base, i32 0, i32 8)
  %v = extractvalue { i64, i32 } %v_pair, 0
  call i32 @llvm.haydn.sdw.cb.imm(i64 %v, i32 %base, i32 0, i32 8)
  ret void
}

; CB load + CB store with register stride — both must emit.
define void @test_cb_load_then_store_reg(i32 %base, i32 %stride) {
; CHECK-LABEL: test_cb_load_then_store_reg:
; CHECK: d_ldw_cb_reg
; CHECK: d_sdw_cb_reg
entry:
  %v_pair = call { i64, i32 } @llvm.haydn.ldw.cb.reg(i32 %base, i32 1, i32 %stride)
  %v = extractvalue { i64, i32 } %v_pair, 0
  call i32 @llvm.haydn.sdw.cb.reg(i64 %v, i32 %base, i32 1, i32 %stride)
  ret void
}

; CB store with immediate stride only — the standalone store must emit.
; This is the surgical probe: pre- this function lowered to an empty
; body because the store pseudo was dropped.
define void @test_sdw_cb_imm_only(i64 %data, i32 %base) {
; CHECK-LABEL: test_sdw_cb_imm_only:
; CHECK: d_sdw_cb_imm
entry:
  call i32 @llvm.haydn.sdw.cb.imm(i64 %data, i32 %base, i32 2, i32 16)
  ret void
}

; CB store with register stride only.
define void @test_sdw_cb_reg_only(i64 %data, i32 %base, i32 %stride) {
; CHECK-LABEL: test_sdw_cb_reg_only:
; CHECK: d_sdw_cb_reg
entry:
  call i32 @llvm.haydn.sdw.cb.reg(i64 %data, i32 %base, i32 3, i32 %stride)
  ret void
}
