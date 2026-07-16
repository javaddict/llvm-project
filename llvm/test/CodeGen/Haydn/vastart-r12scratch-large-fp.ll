; RUN: llc -mtriple=haydn-unknown-elf -O0 -frame-pointer=all < %s | FileCheck %s --check-prefix=FP
; RUN: llc -mtriple=haydn-unknown-elf -O0 < %s | FileCheck %s --check-prefix=OMIT
;
; P0 L2 / Track C: large locals + va_start + frame pointer must not hard-fatal
; in AsmPrinter emitATScratchSaveIfNeeded (R12ScratchFI beyond scaled-simm6).
; ST32/LD32 use RI16 simm16; offsets like ~-1KiB from FP must compile.
; AIE model: R12 allocatable → FP path uses R12ScratchFI spill around VASTART.
;
; Minimal C shape: volatile char pad[1024]; va_start; va_arg; under
; fno-omit-frame-pointer.

define i32 @sum_large_fp(i32 %n, ...) nounwind {
; FP-LABEL: sum_large_fp:
; VASTART brackets: spill/restore R12 via in-frame slot.
; Offset from FP is outside scaled-simm6 but inside simm16 → direct st32/ld32.
; FP: st32{{.*}} r12, {{fp|r14}},
; FP: ld32{{.*}} r12, {{fp|r14}},
; FP: jalr
;
; OMIT-FP still compiles (slot near SP).
; OMIT-LABEL: sum_large_fp:
; OMIT: jalr
entry:
  %pad = alloca [1024 x i8], align 1
  %ap = alloca i8, align 4
  %p0 = getelementptr inbounds [1024 x i8], ptr %pad, i32 0, i32 0
  store volatile i8 1, ptr %p0, align 1
  call void @llvm.va_start(ptr %ap)
  %a = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  %v = load volatile i8, ptr %p0, align 1
  %vz = zext i8 %v to i32
  %r = add i32 %a, %vz
  ret i32 %r
}

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
