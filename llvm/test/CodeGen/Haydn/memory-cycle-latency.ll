; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -debug-only=machine-scheduler < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PRODUCT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -haydn-accurate-memory-latency=false \
; RUN:     -debug-only=machine-scheduler < %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SOFT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s -o - \
; RUN:   | FileCheck %s --check-prefix=PACK-PRODUCT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -haydn-accurate-memory-latency=false < %s -o - \
; RUN:   | FileCheck %s --check-prefix=PACK-SOFT
; REQUIRES: asserts

; Role: semantic — MemoryCycle product pin (architectural mem→mem latency).
;
; Product getMemoryLatency uses First/LastMemoryCycle tables for
; Slot0_LS / Slot1_LD / Slot01_LD → mem→mem Ord Memory latency
; max(1, Last-First+1) = 2 (LoadLatency=2 / OperandCycles [2]).
; Soft soak-off (-haydn-accurate-memory-latency=false) is class-agnostic
; latency 1 for densify A/B only; densify invents remain FATED.
;
; W3 (store mayLoad=0): stores are no longer loads, so store→store is not a
; MemoryCycle edge. PRODUCT/SOFT pin the real store→load (and load→store)
; Order Memory edge on the producer SU, not a later store SU. Disjoint
; store-store may occupy consecutive cycles; aliased store→load stays
; Latency=2 (product) / 1 (soft).

define i32 @store_then_load(ptr %p, i32 %v) {
; PACK-PRODUCT-LABEL: store_then_load:
; PACK-PRODUCT:       // %bb.0: // %entry
; PACK-PRODUCT-NEXT:    { nop; xor32 r0, r0, r0 }
; PACK-PRODUCT-NEXT:    { nop; subi32 sp, sp, 8 }
; PACK-PRODUCT-NEXT:    .cfi_def_cfa_offset 8
; PACK-PRODUCT-NEXT:    { nop; st32 r2, r1, 0 }
; PACK-PRODUCT-NEXT:    { nop; nop }
; PACK-PRODUCT-NEXT:    { nop; ld32 r1, r1, 0 }
; PACK-PRODUCT-NEXT:    { nop; addi32 sp, sp, 8 }
; PACK-PRODUCT:    { nop; jalr r0, lr, 0 }
;
; PACK-SOFT-LABEL: store_then_load:
; PACK-SOFT:       // %bb.0: // %entry
; PACK-SOFT-NEXT:    { nop; xor32 r0, r0, r0 }
; PACK-SOFT-NEXT:    { nop; subi32 sp, sp, 8 }
; PACK-SOFT-NEXT:    .cfi_def_cfa_offset 8
; PACK-SOFT-NEXT:    { nop; st32 r2, r1, 0 }
; PACK-SOFT-NEXT:    { nop; ld32 r1, r1, 0 }
; PACK-SOFT-NEXT:    { nop; addi32 sp, sp, 8 }
; PACK-SOFT:    { nop; jalr r0, lr, 0 }
entry:
  store i32 %v, ptr %p, align 4
  %x = load i32, ptr %p, align 4
  ret i32 %x
}

define void @load_then_store(ptr %p, i32 %v) {
; PACK-PRODUCT-LABEL: load_then_store:
; PACK-PRODUCT:       // %bb.0: // %entry
; PACK-PRODUCT-NEXT:    { nop; xor32 r0, r0, r0 }
; PACK-PRODUCT-NEXT:    { nop; subi32 sp, sp, 8 }
; PACK-PRODUCT-NEXT:    .cfi_def_cfa_offset 8
; PACK-PRODUCT-NEXT:    { nop; ld32 r3, r1, 0 }
; PACK-PRODUCT-NEXT:    { nop; nop }
; PACK-PRODUCT-NEXT:    { nop; add32 r2, r3, r2 }
; PACK-PRODUCT-NEXT:    { nop; st32 r2, r1, 0 }
; PACK-PRODUCT-NEXT:    { nop; addi32 sp, sp, 8 }
; PACK-PRODUCT:    { nop; jalr r0, lr, 0 }
;
; PACK-SOFT-LABEL: load_then_store:
; PACK-SOFT:       // %bb.0: // %entry
; PACK-SOFT-NEXT:    { nop; xor32 r0, r0, r0 }
; PACK-SOFT-NEXT:    { nop; subi32 sp, sp, 8 }
; PACK-SOFT-NEXT:    .cfi_def_cfa_offset 8
; PACK-SOFT-NEXT:    { nop; ld32 r3, r1, 0 }
; PACK-SOFT-NEXT:    { nop; nop }
; PACK-SOFT-NEXT:    { nop; add32 r2, r3, r2 }
; PACK-SOFT-NEXT:    { nop; st32 r2, r1, 0 }
; PACK-SOFT-NEXT:    { nop; addi32 sp, sp, 8 }
; PACK-SOFT:    { nop; jalr r0, lr, 0 }
entry:
  %x = load i32, ptr %p, align 4
  %y = add i32 %x, %v
  store i32 %y, ptr %p, align 4
  ret void
}

define i32 @store_store_load(ptr %p, i32 %a, i32 %b) {
; PACK-PRODUCT-LABEL: store_store_load:
; PACK-PRODUCT:       // %bb.0: // %entry
; PACK-PRODUCT-NEXT:    { nop; xor32 r0, r0, r0 }
; PACK-PRODUCT-NEXT:    { nop; subi32 sp, sp, 8 }
; PACK-PRODUCT-NEXT:    .cfi_def_cfa_offset 8
; PACK-PRODUCT-NEXT:    { nop; st32 r2, r1, 0 }
; PACK-PRODUCT-NEXT:    { nop; nop }
; PACK-PRODUCT-NEXT:    { nop; st32 r3, r1, 1 }
; PACK-PRODUCT-NEXT:    { nop; nop }
; PACK-PRODUCT-NEXT:    { nop; ld32 r2, r1, 0 }
; PACK-PRODUCT-NEXT:    { nop; nop }
; PACK-PRODUCT-NEXT:    { nop; move32 r1, r2 }
; PACK-PRODUCT-NEXT:    { nop; addi32 sp, sp, 8 }
; PACK-PRODUCT:    { nop; jalr r0, lr, 0 }
;
; PACK-SOFT-LABEL: store_store_load:
; PACK-SOFT:       // %bb.0: // %entry
; PACK-SOFT-NEXT:    { nop; xor32 r0, r0, r0 }
; PACK-SOFT-NEXT:    { nop; subi32 sp, sp, 8 }
; PACK-SOFT-NEXT:    .cfi_def_cfa_offset 8
; PACK-SOFT-NEXT:    { nop; st32 r2, r1, 0 }
; PACK-SOFT-NEXT:    { nop; ld32 r2, r1, 0 }
; PACK-SOFT-NEXT:    { nop; st32 r3, r1, 1 }
; PACK-SOFT-NEXT:    { nop; move32 r1, r2 }
; PACK-SOFT-NEXT:    { nop; addi32 sp, sp, 8 }
; PACK-SOFT:    { nop; jalr r0, lr, 0 }
entry:
  store i32 %a, ptr %p, align 4
  %q = getelementptr i32, ptr %p, i32 1
  store i32 %b, ptr %q, align 4
  %x = load i32, ptr %p, align 4
  ret i32 %x
}

; PRODUCT: ScheduleDAGMI::schedule starting
; PRODUCT: ST32{{.*}}store (s32) into %ir.p
; PRODUCT: Ord{{ +}}Latency=2 Memory
; PRODUCT: LD32{{.*}}load (s32) from %ir.p
; PRODUCT: ScheduleDAGMI::schedule starting
; PRODUCT: LD32{{.*}}load (s32) from %ir.p
; PRODUCT: Ord{{ +}}Latency=2 Memory
; PRODUCT: ST32{{.*}}store (s32) into %ir.p
; PRODUCT: ScheduleDAGMI::schedule starting
; PRODUCT: ST32{{.*}}store (s32) into %ir.p
; PRODUCT: Ord{{ +}}Latency=2 Memory
; PRODUCT: LD32{{.*}}load (s32) from %ir.p

; SOFT: ScheduleDAGMI::schedule starting
; SOFT: ST32{{.*}}store (s32) into %ir.p
; SOFT: Ord{{ +}}Latency=1 Memory
; SOFT: LD32{{.*}}load (s32) from %ir.p
; SOFT: ScheduleDAGMI::schedule starting
; SOFT: LD32{{.*}}load (s32) from %ir.p
; SOFT: Ord{{ +}}Latency=1 Memory
; SOFT: ST32{{.*}}store (s32) into %ir.p
; SOFT: ScheduleDAGMI::schedule starting
; SOFT: ST32{{.*}}store (s32) into %ir.p
; SOFT: Ord{{ +}}Latency=1 Memory
; SOFT: LD32{{.*}}load (s32) from %ir.p

