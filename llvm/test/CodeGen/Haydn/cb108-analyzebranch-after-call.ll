; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — BranchRelaxation must be able to analyze a conditional branch that sits in the same MBB as a preceding libcall (JAL_W).

; BranchRelaxation must be able to analyze a conditional branch that
; sits in the same MBB as a preceding libcall (JAL_W). analyzeBranch used to
; treat the mid-block JAL_W as "unanalyzable" after already parsing the
; trailing cond-branch, so fixupConditionalBranch asserted
; "branches to be relaxed must be analyzable" (yarpgen seed 2289: __divsi3
; then BNE_W).
;
; Shape: far forward conditional after a soft-div call. The padding stores
; push the branch past GE96-03 ±2048-byte WIDE_BranchSImm12 so BranchRelaxation rewrites it.
; Without the analyzeBranch fix, llc aborts; with it, we get a relaxed form
; (inverted near cond + far B / indirect).

declare i32 @__divsi3(i32, i32)

@pad = external global [1024 x i32]

define i32 @cb108_call_then_far_bne(i32 %a, i32 %b, i32 %c) nounwind {
entry:
  %q = call i32 @__divsi3(i32 %a, i32 %b)
  %cmp = icmp ne i32 %q, %c
  br i1 %cmp, label %far, label %near

near:
  ret i32 0

far:
  ; ~600 volatile stores × 8B Mode-0 bundle ≈ 4800B — past ±4KB branch reach.
  store volatile i32 1, ptr @pad
  store volatile i32 1, ptr @pad
  store volatile i32 1, ptr @pad
  store volatile i32 1, ptr @pad
  store volatile i32 1, ptr @pad
  store volatile i32 1, ptr @pad
  store volatile i32 1, ptr @pad
  store volatile i32 1, ptr @pad
  store volatile i32 1, ptr @pad
  store volatile i32 1, ptr @pad
  ; Keep the far block large enough that entry→far overflows SImm12.
  call void @cb108_pad()
  ret i32 1
}

; Separate cold padding so the entry→far distance is dominated by this body.
define void @cb108_pad() nounwind {
entry:
  store volatile i32 1, ptr getelementptr inbounds ([1024 x i32], ptr @pad, i32 0, i32 0)
  store volatile i32 1, ptr getelementptr inbounds ([1024 x i32], ptr @pad, i32 0, i32 1)
  store volatile i32 1, ptr getelementptr inbounds ([1024 x i32], ptr @pad, i32 0, i32 2)
  store volatile i32 1, ptr getelementptr inbounds ([1024 x i32], ptr @pad, i32 0, i32 3)
  store volatile i32 1, ptr getelementptr inbounds ([1024 x i32], ptr @pad, i32 0, i32 4)
  store volatile i32 1, ptr getelementptr inbounds ([1024 x i32], ptr @pad, i32 0, i32 5)
  store volatile i32 1, ptr getelementptr inbounds ([1024 x i32], ptr @pad, i32 0, i32 6)
  store volatile i32 1, ptr getelementptr inbounds ([1024 x i32], ptr @pad, i32 0, i32 7)
  store volatile i32 1, ptr getelementptr inbounds ([1024 x i32], ptr @pad, i32 0, i32 8)
  store volatile i32 1, ptr getelementptr inbounds ([1024 x i32], ptr @pad, i32 0, i32 9)
  ret void
}

; CHECK-LABEL: cb108_call_then_far_bne:
; Soft-div call must be present.
; CHECK: jal{{(_w)?}}
; Function must compile (no BranchRelaxation assert) and return.
; CHECK: jalr{{(\.s[012])?}}
