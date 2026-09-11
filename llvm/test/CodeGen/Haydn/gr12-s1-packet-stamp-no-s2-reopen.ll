; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=postmisched -verify-machineinstrs < %s | FileCheck %s --check-prefix=PACK
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=postmisched -debug-only=haydn-post-ra-sched \
; RUN:     -verify-machineinstrs < %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=S1
; REQUIRES: asserts
;
; Role: GR1.2 / GR2.5. S1 leaveMBB installs complete packets and
; leaveFunction expands leftover RET, folds one dest-window stall net,
; leftover-logical inverse-bakes bundled FieldSlot children (JALR_W
; membership), then stamps PostCommitCfgSnapshot before
; PostMachineScheduler returns. GR1.7 deleted the S2 LateConvergence
; driver, so there is no second scheduler to reopen those roots.
; Wrap-only Finalize after stamp still completes inverse on
; post-stamp LBN wraps via fixed complete packet templates and does
; not grow dest-window after the wall.

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

define i32 @gr12_add(i32 %a, i32 %b) nounwind {
; PACK-LABEL: name: gr12_add
; S1 leftover-logical inverse bake: RET is JALR membership, not catalog
; JALR_W as a bundled leftover (CoreMark inverse fatal). Generated JALR
; members must not copy call-shaped caller-saved implicit-defs onto the
; BUNDLE root (FPL/LBN occupancy).
; PACK: ADD32_E{{[23]}}_
; PACK-NOT: {{= ADD32 }}
; PACK-NOT: JALR_W
; PACK-NOT: implicit-def dead $r2
; PACK: JALR_E{{[23]}}_
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @gr12_callee(i32 %x) noinline nounwind {
; PACK-LABEL: name: gr12_callee
; PACK-NOT: JALR_W
; PACK: JALR_E{{[23]}}_
  ret i32 %x
}

define i32 @gr12_call(i32 %x) nounwind {
; PACK-LABEL: name: gr12_call
; CoreMark-shaped returning call packet. S1 wraps JALR_CALL without baking
; onto terminator JALR (encoder peels). ABI clobbers stay on the root.
; PACK-NOT: JALR_W
; PACK: JALR_CALL
; PACK: JALR_E{{[23]}}_
  %r = call i32 @gr12_callee(i32 %x)
  ret i32 %r
}

; S1: HaydnPostRASched S1: stamped PostCommitCfgSnapshot in gr12_add
; S1: HaydnPostRASched S1: stamped PostCommitCfgSnapshot in gr12_callee
; S1: HaydnPostRASched S1: stamped PostCommitCfgSnapshot in gr12_call
; S1-NOT: reopened {{[0-9]+}} provisional BUNDLE
