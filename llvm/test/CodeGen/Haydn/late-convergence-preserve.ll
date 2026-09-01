; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -haydn-sms2 \
; RUN:     -stop-after=haydn-late-convergence -simplify-mir < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=MIR
;
; W68.3R preservation gates under the late-convergence mutators
; (S2 / stalls / HWLoop validate / BranchRelaxation LAST). STATUS
; limit #10: successor lists, profile, debug locations, CFI, kill/dead
; liveness, and MMOs must survive the loop as correctness/observability
; facts, not optional QoR. Default-off (no -haydn-sms2) is the
; byte-identity baseline; -haydn-sms2 is the mutator path.
;
; Shapes chosen not to exhaust the bound (alloca-only/fp-all call
; frames can oscillate; those are a separate no-growth exit).

target triple = "haydn-unknown-elf"

declare void @sink(i32)

define i32 @preserve_diamond(ptr nocapture readonly %a, i32 %n) !dbg !4 {
entry:
  %c = icmp sgt i32 %n, 0, !dbg !8
  br i1 %c, label %then, label %else, !prof !10, !dbg !9
then:
  %x = load i32, ptr %a, align 4, !dbg !11
  %t = add i32 %x, 1, !dbg !12
  br label %join, !dbg !13
else:
  %y = add i32 %n, 7, !dbg !14
  br label %join, !dbg !15
join:
  %r = phi i32 [ %t, %then ], [ %y, %else ]
  ret i32 %r, !dbg !16
}

define i32 @preserve_cfi(i32 %x) {
entry:
  call void @sink(i32 %x)
  %a = add i32 %x, 1
  ret i32 %a
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}
!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: "llvm", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "preserve.c", directory: "/tmp")
!2 = !{i32 2, !"Dwarf Version", i32 4}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "preserve_diamond", scope: !1, file: !1, line: 1, type: !5, isLocal: false, isDefinition: true, scopeLine: 1, flags: DIFlagPrototyped, isOptimized: true, unit: !0)
!5 = !DISubroutineType(types: !6)
!6 = !{!7, !7, !7}
!7 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!8 = !DILocation(line: 2, column: 7, scope: !4)
!9 = !DILocation(line: 2, column: 3, scope: !4)
!10 = !{!"branch_weights", i32 96, i32 4}
!11 = !DILocation(line: 3, column: 7, scope: !4)
!12 = !DILocation(line: 3, column: 11, scope: !4)
!13 = !DILocation(line: 4, column: 3, scope: !4)
!14 = !DILocation(line: 6, column: 7, scope: !4)
!15 = !DILocation(line: 7, column: 3, scope: !4)
!16 = !DILocation(line: 9, column: 3, scope: !4)

; ASM-LABEL: preserve_diamond:
; CFG: both arms survive; the 96:4 weight does not drop a successor.
; ASM: .cfi_startproc
; ASM: .cfi_def_cfa_offset
; ASM: .loc 1 2
; ASM: bnez
; ASM: .loc 1 3
; ASM: jalr
; ASM: .LBB0_2
; ASM: .loc 1 6
; ASM: jalr
; ASM: .cfi_endproc
;
; ASM-LABEL: preserve_cfi:
; Call-frame CFI (offsets are CFA-relative, negative).
; ASM: .cfi_startproc
; ASM: .cfi_def_cfa_offset
; ASM: .cfi_offset {{r8|lr|r15}}, -
; ASM: jal
; ASM: jalr
; ASM: .cfi_endproc

; MIR-LABEL: name: preserve_diamond
; Profile hex is the 96:4 MachineBranchProbability encoding of !prof !10.
; MIR: bb.0.entry:
; MIR: successors: %bb.1(0x7ae147ae), %bb.2(0x051eb852)
; Live-ins on the taken and not-taken arms (post-RA stand-in for PHIs).
; MIR: liveins: $r1, $r2, $r0
; MIR: CFI_INSTRUCTION def_cfa_offset
; MIR: debug-location !8
; MIR: BNEZ{{.*}}debug-location !9
; MIR: bb.1.then:
; MIR: liveins: $r1
; Load MMO and its debug loc survive S2 reopen/reschedule.
; MIR: debug-location !11{{.*}}:: (load (s32) from %ir.a)
; Kill on the last use of the return value.
; MIR: JALR{{.*}}implicit killed $r1
; MIR: bb.2.else:
; MIR: liveins: $r2
; MIR: debug-location !14
; MIR: JALR{{.*}}implicit killed $r1
;
; MIR-LABEL: name: preserve_cfi
; MIR: CFI_INSTRUCTION def_cfa_offset
; MIR: CFI_INSTRUCTION offset $r8, -4
; MIR: CFI_INSTRUCTION offset $r15, -8
; Call clobber liveness: dead implicit-defs plus the killed arg.
; MIR: implicit-def dead $r2
; MIR: implicit killed $r1
; MIR: CFI_INSTRUCTION def_cfa $r13, 0
; MIR: JALR{{.*}}implicit killed $r1
