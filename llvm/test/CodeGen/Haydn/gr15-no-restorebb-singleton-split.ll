; REQUIRES: haydn-registered-target
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfo.cpp --check-prefix=TII
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnLongBranchNormalize.cpp --check-prefix=LBN
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -o /dev/null < %s
;
; GR1.5 B: branch/call phase last via fixed in-block templates.
; insertIndirectBranch leaves RestoreBB empty (generic BR erases it at
; BranchRelaxation.cpp:687-688). No R11 spill. eraseSelectedBranch has
; no singleton-split fallback. LBN is the post-stamp owner; pre-S1 BR
; trampoline stays. Peer: RISC-V RestoreBB (RISCVInstrInfo.cpp:1433-1498)
; is unused; AIE empty PreEmit (AIE2TargetMachine.cpp:92); Hexagon
; replaceWithNop (HexagonConstPropagation.cpp:2508-2512); ARM ImmBranch
; (ARMConstantIslandPass.cpp:184-197).
;
; TII: Haydn eraseSelectedBranch: singleton-split fallback
; TII: RestoreBB stays empty so BranchRelaxation.cpp:687-688 erases it
; TII: no R11 spill, no RestoreBB reload
; TII-NOT: emitWithManualSpill
; TII-NOT: JumpToRestore
; TII-NOT: spill R11 to that FI
; TII-NOT: post-stamp singleton-split
; TII-NOT: Singleton-split fallback is legal only before
;
; LBN: leave for BR trampoline (no RestoreBB)
; LBN: skip re-promote Rank>=LongTemplate
; LBN: padInternalMBBAlignment(MF, TII,
; LBN: layout-site Rank consult
; LBN-NOT: defer to BR trampoline/RestoreBB
; LBN-NOT: BR RestoreBB / trampoline
; LBN-NOT: BR trampoline / RestoreBB

define i32 @gr15_inblock(i32 %a, i32 %b) {
entry:
  %c = icmp eq i32 %a, 0
  br i1 %c, label %far, label %near
near:
  %s = add i32 %b, 1
  br label %exit
far:
  %t = add i32 %b, 2
  br label %exit
exit:
  %r = phi i32 [ %s, %near ], [ %t, %far ]
  ret i32 %r
}
