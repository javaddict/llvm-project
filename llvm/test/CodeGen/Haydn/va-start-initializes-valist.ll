; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; REGRESSION TEST: VASTART must emit the 5-field va_list initialization.
;
; Bug: HaydnAsmPrinter's VASTART case guarded the va_list init with
; if (GprFI < 0 || DrFI < 0 || StackFI < 0) return;
; but HaydnCallLowering::saveVarArgRegisters creates the save areas via
; MachineFrameInfo::CreateFixedObject, which by LLVM API contract returns
; NEGATIVE frame indices (fixed objects live below the locals). The `-1`
; "unset" sentinel therefore collided with every legitimately-created save
; area, the guard false-triggered on EVERY variadic function, and the entire
; 5-field va_list init was silently skipped. As a result G_VAARG read
; uninitialized stack and returned garbage (typically 0).
;
; Fix : gate VASTART on HaydnMachineFunctionInfo::hasVarArgsSaveAreas
; an explicit bool set in saveVarArgRegisters — instead of the FI<0
; sentinel. If this regresses, the CHECKs below will fail because no
; va_list-init stores get emitted near va_start.
;
; Test design: minimal 4-line variadic function. The first named arg (R1)
; consumes the only GPR arg reg, so all variadic GPRs come from the overflow
; stack region; the function MUST still emit the 5 va_list fields (stack base
; gr_top, vr_top, gr_offs, vr_offs) at the va_start site. We check for the
; ST32 writes to the va_list struct (the materialized pointers and the
; negated sizes) — the missing-init bug produced NONE of these.

define dso_local i32 @vone(i32 %n,...) nounwind {
; CHECK-LABEL: vone:
; Va_list init: store __stack (overflow base), __gr_top, __vr_top (3 pointer
; fields). The bug emitted ZERO of these.
; CHECK: st32 {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK: st32 {{r[0-9]+}}, {{r[0-9]+}}, 4
; CHECK: st32 {{r[0-9]+}}, {{r[0-9]+}}, 8
; Va_list init: store __gr_offs and __vr_offs (negated bank sizes) at @12/@16.
; CHECK: st32 {{r[0-9]+}}, {{r[0-9]+}}, 12
; CHECK: st32 {{r[0-9]+}}, {{r[0-9]+}}, 16
entry:
 %ap = alloca i8, align 4
 call void @llvm.va_start(ptr %ap)
 %a = va_arg ptr %ap, i32
 call void @llvm.va_end(ptr %ap)
 ret i32 %a
}

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)
