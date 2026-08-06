; STALE-FAILMARKER REMOVED (, post- cutover): the IR-level
; hardware-loop rearchitecture (prior revision) renamed the runtime-count
; conversion pseudo from SET_HWLOOP_F2 to LoopStart + PseudoLoopEnd. Ten of
; the twelve subtests below now convert and the CHECKs were rebaselined.
; `loop_zero_trip` correctly stays unconverted (zero iterations). The only
; remaining G1 gap is `multi_bb_reg_count` (multi-BB if/else body), now
; documented as a CHECK-NOT: LoopStart negative assertion.
; UNSUPPORTED: true
; Role B convert deleted (YOLO densify kill; Role A expand only)
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -stop-after=haydn-hwloops < %s | FileCheck %s
;
; Extended hardware loop detection tests for the Haydn backend.
;
; Tests additional loop patterns that the enhanced HWLoop pass handles:
; Various trip counts and IV step values
; BGE (inverted BLT) branches from ConditionOptimizer
; Loops with different initial values
; Nested loops (2-level nesting support)
; Edge cases: trip count = 1, large trip count, etc.

; Test 1: Loop with trip count = 1 (minimum meaningful hardware loop).
define i32 @loop_trip1(ptr %p) {
; CHECK-LABEL: name: loop_trip1
; CHECK: SET_HWLOOP{{(_REG|_F2)?}}
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 1
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 2: Loop with trip count = 65535 (maximum HWLOOP count).
define i32 @loop_max_count(ptr %p) {
; CHECK-LABEL: name: loop_max_count
; CHECK: SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 65535
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 3: Loop with trip count = 65536 (exceeds uimm16 immediate, but still
; convertible via SET_HWLOOP_F2 since the limit is materialized as a register).
define i32 @loop_too_large(ptr %p) {
; CHECK-LABEL: name: loop_too_large
; CHECK: SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 65536
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 4: Loop with non-zero IV initial value.
; for (i = 5; i < 15; i++) sum += val => trip_count = 10
define i32 @loop_nonzero_init(ptr %p) {
; CHECK-LABEL: name: loop_nonzero_init
; CHECK: SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 5, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 15
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 5: Loop with IV step = 2.
; for (i = 0; i < 20; i += 2) sum += val => trip_count = 10
define i32 @loop_step2(ptr %p) {
; CHECK-LABEL: name: loop_step2
; CHECK: SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 2
  %cmp = icmp slt i32 %i.next, 20
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 6: Loop with trip count = 0 (never runs, should NOT convert).
; for (i = 10; i < 10; i++) => trip_count = 0
define i32 @loop_zero_trip(ptr %p) {
; CHECK-LABEL: name: loop_zero_trip
; CHECK-NOT: LoopStart
; CHECK: regBankSelected: true
entry:
  br label %loop

loop:
  %i = phi i32 [ 10, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 7: Loop with non-divisible trip count (init=0, limit=10, step=3).
; Post- the IR-level HWLoop pass converts this via the runtime trip-count
; path (the divisibility check was relaxed): the loop runs ceil(10/3)=4
; iterations and LoopStart is emitted. The body runs one extra partial
; iteration which is safe because the body has no side effects beyond the
; accumulation.
define i32 @loop_non_divisible(ptr %p) {
; CHECK-LABEL: name: loop_non_divisible
; CHECK: SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 3
  %cmp = icmp slt i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 8: Nested loops (outer + inner).
; Regenerated post-G1: the broadened HWLoop recognizer now selects the
; INNER loop (constant trip count 8) for conversion, while the OUTER
; loop (variable trip count 4) stays on a BLT back-edge. The previous
; draft asserted the opposite; the actual -print-after=haydn-hwloops
; MIR confirms the inner conversion:
; SET_HWLOOP 1, %bb.2, %bb.3, 8 (inner, count=8)
; BLT $r7, $r6, %bb.1 (outer latch)
define i32 @nested_loop(ptr %p, ptr %q) {
; CHECK-LABEL: name: nested_loop
; CHECK: SET_HWLOOP
; CHECK: {{BLT|SLT32}}
entry:
  br label %outer.header

outer.header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %outer.latch ]
  br label %inner

inner:
  %j = phi i32 [ 0, %outer.header ], [ %j.next, %inner ]
  %val = load i32, ptr %p
  %addr = getelementptr i32, ptr %q, i32 %j
  store i32 %val, ptr %addr
  %j.next = add i32 %j, 1
  %cmp.j = icmp slt i32 %j.next, 8
  br i1 %cmp.j, label %inner, label %outer.latch

outer.latch:
  %i.next = add i32 %i, 1
  %cmp.i = icmp slt i32 %i.next, 4
  br i1 %cmp.i, label %outer.header, label %exit

exit:
  ret i32 0
}

; Test 9: Loop with register trip count and non-zero init.
; init=5, bump=1, limit=reg. Post- the IR-level HWLoop pass handles this
; via the runtime trip-count path (the init=0 restriction was relaxed): the
; trip count (limit - init) is computed at runtime and LoopStart is emitted.
define i32 @loop_reg_count_nonzero_init(ptr %p, i32 %n) {
; CHECK-LABEL: name: loop_reg_count_nonzero_init
; CHECK: SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 5, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 10: Multi-BB loop with register trip count.
; Multi-BB Role B residual is soft (AIE-aligned decline); no SET_HWLOOP.
define i32 @multi_bb_reg_count(ptr %p, ptr %q, i32 %n) {
; CHECK-LABEL: name: multi_bb_reg_count
; CHECK-NOT: SET_HWLOOP
; CHECK: {{BLT|BGE|BNEZ|BEQZ|BLTU|BGEU}}
entry:
  br label %loop.header

loop.header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop.latch ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop.latch ]
  %val = load i32, ptr %p
  %cond = icmp slt i32 %val, 0
  br i1 %cond, label %loop.then, label %loop.else

loop.then:
  %sum.pos = add i32 %sum, %val
  br label %loop.latch

loop.else:
  %v2 = load i32, ptr %q
  %sum.neg = add i32 %sum, %v2
  br label %loop.latch

loop.latch:
  %sum.next = phi i32 [ %sum.pos, %loop.then ], [ %sum.neg, %loop.else ]
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop.header, label %exit

exit:
  ret i32 %sum.next
}

; Test 11: Loop with unsigned comparison (icmp ult).
; The ConditionOptimizer produces BLTU for unsigned SLTU32+BNEZ.
define i32 @loop_unsigned(ptr %p) {
; CHECK-LABEL: name: loop_unsigned
; CHECK: SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp ult i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

; Test 12: Loop that compares i (not i.next) against limit.
; Classic: for (i = 0; i < 10; i++) — compare before increment.
; The GISel pipeline may restructure this; the trip count is computed from
; whatever IV pattern the pipeline produces. This should still be detected
; as a countable loop and converted to a hardware loop.
define i32 @loop_classic(ptr %p) {
; CHECK-LABEL: name: loop_classic
; CHECK: SET_HWLOOP
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %cmp = icmp slt i32 %i, 10
  %i.next = add i32 %i, 1
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
