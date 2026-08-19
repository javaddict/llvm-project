; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
;
; Role: semantic — MOVT32 dest-read occupies a GPR read port even when LLVM
; marks the tied `$rd_src` <undef>.
;
; gcc-c-torture pr53645 (sq1428) strength-reduces v4i32 sdiv by {1,4,2,8}
; to ADD32 + MOVT32. Golden MOVT32 GPR_Read_Port is rt, rs1, rs2 (3R).
; ADD32 is 2R. 5R exceeds the GPR 4R ceiling. Skipping the undef tied
; dest-read under-counted MOVT32 as 2R so the pair packed as 4R; BundleSim
; catalogs 3R and rejected the parcel (ISSUE_CONFLICT).
;
; CHECK the two opcodes do not share a printed Format E cycle. Sequential
; `{ nop; add32 ... }` / `{ nop; movt32 ... }` parcels are legal.

define void @sq1428(ptr %x, ptr %y) {
; CHECK-LABEL: sq1428:
; CHECK-NOT: { {{.*}}add32{{.*}}movt32
; CHECK-NOT: { {{.*}}movt32{{.*}}add32
; CHECK: add32
; CHECK: movt32
  %a = load <4 x i32>, ptr %y, align 16
  %d = sdiv <4 x i32> %a, <i32 1, i32 4, i32 2, i32 8>
  store <4 x i32> %d, ptr %x, align 16
  ret void
}
