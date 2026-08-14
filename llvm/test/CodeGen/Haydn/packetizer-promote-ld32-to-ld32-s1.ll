; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=postmisched < %s | FileCheck %s

; Role: MIR — .3/4: dual independent LD32 pack via HR tryAddProduct → setDesc (AIEMachineScheduler.cpp:1121-1132 materializeMultiOpcodeInstrs.

; B3.exit.3/4: dual independent LD32 pack via HR tryAddProduct → setDesc
; (AIEMachineScheduler.cpp:1121-1132 materializeMultiOpcodeInstrs;
; AIEHazardRecognizer.cpp:389 setAlternateDescriptor). Post-pack MIR shows
; durable Format E members (LOADSTORE0 + LOAD1), not logical-only LD32.
;
; Load-bearing: one LOADSTORE0 member and one LOAD1 member.

@g1 = external global i32, align 4
@g2 = external global i32, align 4

define i32 @two_independent_loads(i32 %a) nounwind {
  ; CHECK-LABEL: name: two_independent_loads
  ; CHECK-DAG: S_LW_WITH_IMM_E{{[23]}}_E{{[0-2]}}_LOADSTORE0_
  ; CHECK-DAG: S_LW_WITH_IMM_E{{[23]}}_E{{[0-2]}}_LOAD1_
  %p1 = load i32, ptr @g1, align 4
  %p2 = load i32, ptr @g2, align 4
  %sum = add i32 %p1, %p2
  %r = add i32 %sum, %a
  ret i32 %r
}

; Second probe: two loads through distinct incoming pointer arguments.
; setDesc materializes slot members when co-issued.
define i32 @two_ptr_loads(ptr %a, ptr %b) nounwind {
  ; CHECK-LABEL: name: two_ptr_loads
  ; CHECK-DAG: S_LW_WITH_IMM_E{{[23]}}_E{{[0-2]}}_LOADSTORE0_
  ; CHECK-DAG: S_LW_WITH_IMM_E{{[23]}}_E{{[0-2]}}_LOAD1_
  %va = load i32, ptr %a, align 4
  %vb = load i32, ptr %b, align 4
  %sum = add i32 %va, %vb
  ret i32 %sum
}
