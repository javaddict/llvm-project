; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=postmisched < %s | FileCheck %s
;
; B3.exit.3/4: dual independent loads pack via HR tryAddProduct → setDesc
; (AIEMachineScheduler.cpp:1121-1132 materializeMultiOpcodeInstrs;
; AIEHazardRecognizer.cpp:389 setAlternateDescriptor). Post-pack MIR shows
; durable format members, not the logical-only spelling.
;
; The behaviour survived the format E switch; the REASON did not. Bundle128
; had one load unit per slot, so packing two loads meant promoting the second
; to another slot and the test was named for it. Format E has two load units,
; LOADSTORE0 and LOAD1, and an entry may name either, so what has to be true
; is that the two loads take DIFFERENT UNITS. The checks say that directly.
; The position is the packer's choice and is deliberately not pinned.
;
; The spelling changed too: § 5.6's load/store rename made LD32 into
; `s_lw_with_imm rt, rs, imm` with an operand it did not have before.
;
; Load-bearing: both load units are used, in one bundle.

@g1 = external global i32, align 4
@g2 = external global i32, align 4

define i32 @two_independent_loads(i32 %a) nounwind {
  ; CHECK-LABEL: name: two_independent_loads
  ; CHECK: BUNDLE
  ; CHECK-DAG: S_LW_WITH_IMM_P{{[0-9]+}}_LOADSTORE0
  ; CHECK-DAG: S_LW_WITH_IMM_P{{[0-9]+}}_LOAD1
  %p1 = load i32, ptr @g1, align 4
  %p2 = load i32, ptr @g2, align 4
  %sum = add i32 %p1, %p2
  %r = add i32 %sum, %a
  ret i32 %r
}

; Second probe: two loads through distinct incoming pointer arguments.
; setDesc materializes unit-bearing members when co-issued.
define i32 @two_ptr_loads(ptr %a, ptr %b) nounwind {
  ; CHECK-LABEL: name: two_ptr_loads
  ; CHECK: BUNDLE
  ; CHECK-DAG: S_LW_WITH_IMM_P{{[0-9]+}}_LOADSTORE0
  ; CHECK-DAG: S_LW_WITH_IMM_P{{[0-9]+}}_LOAD1
  %va = load i32, ptr %a, align 4
  %vb = load i32, ptr %b, align 4
  %sum = add i32 %va, %vb
  ret i32 %sum
}
