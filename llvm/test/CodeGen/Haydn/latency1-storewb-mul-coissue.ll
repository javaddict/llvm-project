; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:   -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: golden Data_Latency=1 admission for store-writeback
; registers and fresh-dest multiplies (2026-08-21 latency P3,
; gaps/audit_latency.md mismatch #3/#4 + the post-landing correction).
;
; Bug: both families were pinned at the conservative latency-2 scaffold
; (Slot0_LS / Slot12_MAC wb shape), so every dependent chain modeled one
; extra cycle and the scheduler inserted an all-NOP stall bundle between
; producer and next-bundle consumer. Golden instruction_type_index pins
; Data_Latency=1 for the store writeback register (only the loaded value
; of the load siblings is latency 2) and for the fresh-dest multiplies
; (the lat-1 set derived per-row: FMUL16_*/FMUL32X16_*/MUL32X16_*/
; MUL64_*/MULL/MULSSH/X2MUL32/...; X4MUL16/X2FMUL32*/X2CMUL32X16*/and
; every accumulator-tied row are golden lat-2 and stay 2).
;
; Fix: Slot0_LS_WbLat (LOADSTORE0, OperandCycles [1], MemoryCycle pair
; unchanged) and Slot12_MAC_MulLat + per-slot member rows (OperandCycles
; [1,1,1,1]), published by FormatE/generate_sched_records.py from the
; per-row golden admission (generate_format_e_records.py loaders — never
; a family list).
;
; Test design: one dependent chain per family. Before the fix each chain
; had an all-NOP stall parcel between producer and consumer (see the
; pre-change asm in the task report: `{ nop; nop }` between mul64.ll and
; add64, and between s_sw_post_imm and add32). After the fix the consumer
; issues in the very next parcel — CHECK-NEXT fails if either the latency
; regresses to 2 (stall parcel returns) or collapses below 1 (consumer
; would share the producer parcel — impossible for a true-RAW chain under
; the no-forwarding law; HaydnIntraCycleRAW keeps them apart regardless
; of itinerary latency).

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

declare i64 @llvm.haydn.mul64.ll(i32, i32)
declare ptr @llvm.haydn.s.sw.post.imm(i32, ptr, i32)

; Fresh-dest multiply feeding an add in the next bundle: no stall parcel.
; CHECK-LABEL: mul64_chain:
; CHECK: { 	nop; 	mul64.ll
; CHECK-NEXT: { 	nop; 	nop; 	add64
define i64 @mul64_chain(i32 %a, i32 %b) nounwind {
  %m = call i64 @llvm.haydn.mul64.ll(i32 %a, i32 %b)
  %s = add i64 %m, %m
  ret i64 %s
}

; Store-writeback pointer feeding an add in the next bundle: no stall
; parcel. The writeback REGISTER is latency 1; the stored data path is
; unaffected (memory-edge pair unchanged on the WbLat class).
; CHECK-LABEL: store_wb_chain:
; CHECK: { 	nop; 	s_sw_post_imm
; CHECK-NEXT: { 	nop; 	add32
define i32 @store_wb_chain(ptr %p, i32 %v) nounwind {
  %q = call ptr @llvm.haydn.s.sw.post.imm(i32 %v, ptr %p, i32 4)
  %i = ptrtoint ptr %q to i32
  %x = add i32 %i, %v
  ret i32 %x
}
