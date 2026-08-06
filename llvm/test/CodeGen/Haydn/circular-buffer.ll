; UNSUPPORTED: true
; Role: retired — CircularBuffer analysis pass deleted; not product coverage.
; Do not count as product green. RUN is deliberately false so a dropped
; RUN: false

; Circular buffer detection tests for the Haydn backend.
;
; The HaydnCircularBuffer pass detects AND-with-mask patterns where the mask
; is of the form 2^k - 1 (power of 2 minus 1) and the result flows into a
; load/store address. This identifies circular buffer access patterns like:
;
; idx = (idx + 1) & (N - 1); // N is power of 2
; val = buf[idx];
;
; We use -stop-after=haydn-circular-buffer to check MIR-level output.
; The pass is analysis-only (no code transformation).
;
; The codegen pattern for circular buffer access is:
; ADDI32 r0, <mask>; materialize mask constant (e.g. 15)
; AND32 rIdx, rMask; apply circular buffer mask
; SLL32 rIdx, rScale; idx * scale (shift, since scale is power of 2)
; ADD32 rIdx, rBase; addr = idx * scale + base
; LD32 rAddr, 0; load from computed address

;Test 1: Circular buffer with mask 15 (buffer size 16).

@buf_16 = global [16 x i32] zeroinitializer
@idx_16 = global i32 0

define i32 @circular_buf_16() {
; CHECK-LABEL: name: circular_buf_16
entry:
  %idx = load i32, ptr @idx_16
  %masked = and i32 %idx, 15
  %ptr = getelementptr [16 x i32], ptr @buf_16, i32 0, i32 %masked
  %val = load i32, ptr %ptr
  ret i32 %val
; CHECK: AND32
; CHECK: SLL32
; CHECK: LD32
}

;Test 2: Circular buffer with mask 7 (buffer size 8).
@buf_8 = global [8 x i32] zeroinitializer
@idx_8 = global i32 0

define i32 @circular_buf_8() {
; CHECK-LABEL: name: circular_buf_8
entry:
  %idx = load i32, ptr @idx_8
  %masked = and i32 %idx, 7
  %ptr = getelementptr [8 x i32], ptr @buf_8, i32 0, i32 %masked
  %val = load i32, ptr %ptr
  ret i32 %val
; CHECK: AND32
; CHECK: SLL32
; CHECK: LD32
}

;Test 3: Circular buffer write (store) with mask 31 (buffer size 32).
@buf_32 = global [32 x i32] zeroinitializer
@idx_32 = global i32 0

define void @circular_buf_store_32(i32 %val) {
; CHECK-LABEL: name: circular_buf_store_32
entry:
  %idx = load i32, ptr @idx_32
  %masked = and i32 %idx, 31
  %ptr = getelementptr [32 x i32], ptr @buf_32, i32 0, i32 %masked
  store i32 %val, ptr %ptr
  ret void
; CHECK: AND32
; CHECK: SLL32
; CHECK: ST32
}

;Test 4: Circular buffer with mask 63 (buffer size 64).
@buf_64 = global [64 x i32] zeroinitializer
@idx_64 = global i32 0

define i32 @circular_buf_64() {
; CHECK-LABEL: name: circular_buf_64
entry:
  %idx = load i32, ptr @idx_64
  %masked = and i32 %idx, 63
  %ptr = getelementptr [64 x i32], ptr @buf_64, i32 0, i32 %masked
  %val = load i32, ptr %ptr
  ret i32 %val
; CHECK: AND32
; CHECK: SLL32
; CHECK: LD32
}

;Test 5: Circular buffer with mask 255 (buffer size 256).
@buf_256 = global [256 x i32] zeroinitializer
@idx_256 = global i32 0

define i32 @circular_buf_256() {
; CHECK-LABEL: name: circular_buf_256
entry:
  %idx = load i32, ptr @idx_256
  %masked = and i32 %idx, 255
  %ptr = getelementptr [256 x i32], ptr @buf_256, i32 0, i32 %masked
  %val = load i32, ptr %ptr
  ret i32 %val
; CHECK: AND32
; CHECK: SLL32
; CHECK: LD32
}

;Test 6: Non-circular-buffer AND (mask is NOT power-of-2 minus 1).
; AND with 6 (binary 110) is NOT a circular buffer mask (6+1=7, not power of 2).
; The pass should NOT count this as a circular buffer pattern.
define i32 @not_circular_buf(i32 %idx, ptr %buf) {
; CHECK-LABEL: name: not_circular_buf
entry:
  %masked = and i32 %idx, 6
  %ptr = getelementptr i32, ptr %buf, i32 %masked
  %val = load i32, ptr %ptr
  ret i32 %val
; The AND still exists but mask=6 is not 2^k-1, so no circular buffer detection.
; CHECK: AND32
; CHECK: SLL32
; CHECK: LD32
}

;Test 7: AND with power-of-2 minus 1 but no load/store use.
; The AND result is returned directly, not used as a memory address.
; The mask 15 IS valid, but the function just returns — no LD32/ST32
; appear in the function body between AND32 and JALR.
define i32 @and_no_mem(i32 %x) {
; CHECK-LABEL: name: and_no_mem
entry:
  %masked = and i32 %x, 15
  ret i32 %masked
; The function body has AND32 followed by JALR (return), no memory ops.
; CHECK: AND32
; CHECK: JALR
}
