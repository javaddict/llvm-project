; RUN: llc -mtriple=haydn-unknown-elf -O0 < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s --check-prefix=O2
;
; Haydn has an EXPOSED pipeline: no interlock. A Data_Latency = 2 def (loads,
; CSRR, MAC) must not be read in the next bundle. Nothing enforced this before
; HaydnLatencyStalls:
;
;   * -O0 functions are optnone, so PostMachineScheduler AND
;     HaydnFinalizeBundle both skipFunction — nothing schedules at all
;   * HaydnSubtarget::adjustSchedDependency deliberately softens load->use to
;     latency 1 as a scheduling heuristic
;
; BundleSim cannot catch a violation either: it is a functional bundle simulator
; with no timing model, so violating code still returns the right answer in
; simulation and the wrong one on hardware.
;
; A load feeding the very next instruction must get a stall bundle between them.
; With nothing else to fill the slot, that is an all-NOP bundle at both -O0 and
; -O2 (the -O2 scheduler softens load latency to 1, so the backstop is what
; separates them).

; O0-LABEL: load_then_use:
; O0:      ld32
; O0-NEXT: nop; {{.*}}nop; {{.*}}nop
; O2-LABEL: load_then_use:
; O2:      ld32
; O2-NEXT: nop; {{.*}}nop; {{.*}}nop
define i32 @load_then_use(ptr %p) nounwind {
  %v = load i32, ptr %p, align 4
  %s = add i32 %v, %v
  ret i32 %s
}

; The base writeback of a post-increment load is a destination too: the ISA
; documents one Data_Latency per instruction and lists both rt and rs as write
; ports, so a pointer walk must not read the updated base in the next bundle.
; O0-LABEL: postinc_walk:
; O0: ld
define i32 @postinc_walk(ptr %p, i32 %n) nounwind {
entry:
  br label %loop

loop:
  %ptr = phi ptr [ %p, %entry ], [ %next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %sum, %loop ]
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %b = load i8, ptr %ptr, align 1
  %z = zext i8 %b to i32
  %sum = add i32 %acc, %z
  %next = getelementptr inbounds i8, ptr %ptr, i32 1
  %inc = add i32 %i, 1
  %done = icmp eq i32 %inc, %n
  br i1 %done, label %exit, label %loop

exit:
  ret i32 %sum
}
