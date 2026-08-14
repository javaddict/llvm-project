; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -filetype=obj < %s | llvm-objdump -d - | \
; RUN:     FileCheck %s --check-prefix=OBJ
;
; REGRESSION TEST: tied-def accumulator MAC must encode/decode round-trip
; with acc and dst forced to the same physical register.
;
; Bug (//codex §1A): the prior selectAccMAC seeded the accumulator
; value via `OR64 Tmp, Acc, Acc` then emitted `<MAC> DstReg, Src1, Src2
; implicit-use Tmp`. The implicit-use operand was dropped by the encoder, and
; nothing forced the regalloc to assign Tmp and DstReg to the same physical
; register. Silicon read rtd from the destination's physical register, which
; held whatever was there — silent miscompute for ALL accumulator-form MACs.
;
; Fix : every accumulator-form MAC def in HaydnInstrInfoAuto.td uses
; FmtALU64Acc with `let Constraints = "$rd = $rd_in"`. selectAccMAC emits a
; single MCInst with the tied-def constraint, so regalloc must coalesce Acc
; and DstReg to the same physical register; the encoded destination field
; holds the accumulator value at silicon time.
;
; Test design: the MIR check verifies (a) no OR64 seed and (b) the MAC opcode
; appears with the accumulator operand tied to the destination. The objdump
; check verifies the MC encoder writes a valid encoding that the disassembler
; reconstructs back to the same MAC opcode (no encoder/decoder drift). If the
; tied-def constraint regresses (e.g. reverting selectAccMAC to OR64+implicit
; or dropping Constraints from the.td), the MIR check fails on CHECK-NOT: or64
; and the objdump check may produce a different disassembly (or the encoder may
; crash on a 4-operand MCInst whose td only declares 3).
;
; References:
; ~/haydn-plans/decisions/-accumulator-mac-tied-def-implementation.md
; ~/haydn-plans/decisions/-accumulator-mac-tied-def-modeling.md
; ~/haydn-plans/lessons/selectaccmac-implicit-use-not-silicon-correct.md
; ~/haydn-plans/lessons/mac-tied-def-implementation.md
; spec: Database/haydn_instruction_db.json — every MAC listed has
; slots.1.DR_Read_Port = [rsd1, rsd2, rtd]

declare i64 @llvm.haydn.mula64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.muls64.ss.ll(i64, <2 x i32>, <2 x i32>)
declare i64 @llvm.haydn.ff2mula32rs.lh(i64, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32>, <2 x i32>, <2 x i32>)
; MIR-LABEL: test_mula64_ll:
; MIR-NOT: or64
; MIR:     mula64_ll
; OBJ-LABEL: <test_mula64_ll>:
; OBJ:     mula64_ll
define i64 @test_mula64_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.1 = bitcast i64 %a to <2 x i32>
  %bc.2 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.mula64.ss.ll(i64 %acc, <2 x i32> %bc.1, <2 x i32> %bc.2)
  ret i64 %r
}

; MIR-LABEL: test_muls64_ll:
; MIR-NOT: or64
; MIR:     muls64_ll
; OBJ-LABEL: <test_muls64_ll>:
; OBJ:     muls64_ll
define i64 @test_muls64_ll(i64 %acc, i64 %a, i64 %b) {
  %bc.3 = bitcast i64 %a to <2 x i32>
  %bc.4 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.muls64.ss.ll(i64 %acc, <2 x i32> %bc.3, <2 x i32> %bc.4)
  ret i64 %r
}

; MIR-LABEL: test_ff2mula32rs_lh:
; MIR-NOT: or64
; MIR-NOT: f2mulaa32rs
; MIR:     ff2mula32rs_lh
; OBJ-LABEL: <test_ff2mula32rs_lh>:
; OBJ:     ff2mula32rs_lh
define i64 @test_ff2mula32rs_lh(i64 %acc, i64 %a, i64 %b) {
  %bc.5 = bitcast i64 %a to <2 x i32>
  %bc.6 = bitcast i64 %b to <2 x i32>
  %r = call i64 @llvm.haydn.ff2mula32rs.lh(i64 %acc, <2 x i32> %bc.5, <2 x i32> %bc.6)
  ret i64 %r
}

; MIR-LABEL: test_x2fcmula32rs:
; MIR-NOT: or64
; MIR:     x2fcmula32rs
; OBJ-LABEL: <test_x2fcmula32rs>:
; OBJ:     x2fcmula32rs
define i64 @test_x2fcmula32rs(i64 %acc, i64 %a, i64 %b) {
  %bc.7 = bitcast i64 %acc to <2 x i32>
  %bc.8 = bitcast i64 %a to <2 x i32>
  %bc.9 = bitcast i64 %b to <2 x i32>
  %call.10 = call <2 x i32> @llvm.haydn.x2fcmula32rs(<2 x i32> %bc.7, <2 x i32> %bc.8, <2 x i32> %bc.9)
  %r = bitcast <2 x i32> %call.10 to i64
  ret i64 %r
}
