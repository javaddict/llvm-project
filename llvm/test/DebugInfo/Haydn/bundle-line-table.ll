; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -filetype=obj -o %t.o < %s
; RUN: llvm-dwarfdump -debug-line %t.o | FileCheck %s --check-prefix=LINE
; RUN: llvm-dwarfdump -debug-info %t.o | FileCheck %s --check-prefix=INFO
;
; REGRESSION TEST: -O2 -g line tables / is_stmt survive VLIW BUNDLE wrap (T-MC7).
;
; Bug: BUNDLE roots kept empty DebugLoc (HaydnFinalizeBundle skipped already-
; bundled packs). DwarfDebug records loc only on the top-level MI, so bundled
; loops emitted line-0 parcels. MinInstAlignment=EncodedBytes (12) also
; divided address deltas by 12, corrupting any address not ≡0 mod 12.
; Fix: propagate earliest member DebugLoc onto empty BUNDLE roots; set
; MinInstAlignment to golden two-byte min bundle-address align (not
; EncodedBytes, not Align=4).
;
; Test design: a loop with distinct source lines plus a dbg.value. FileCheck
; pins min_inst_length=2, is_stmt on the loop body line, and that the
; local's DW_AT_location survived. Do not pin parcel PCs (schedule-dependent).

source_filename = "bundle_line.c"
target datalayout = "e-m:e-p:32:32-i64:64-v128:32:128-n32"
target triple = "haydn-unknown-elf"

define i32 @bundled_loop(ptr %p, i32 %n, i32 %a, i32 %b) !dbg !7 {
entry:
  br label %loop, !dbg !16

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  call void @llvm.dbg.value(metadata i32 %s, metadata !13, metadata !DIExpression()), !dbg !17
  %pi = getelementptr inbounds i32, ptr %p, i32 %i, !dbg !18
  %v = load i32, ptr %pi, align 4, !dbg !18
  %t0 = add i32 %v, %a, !dbg !19
  %t1 = add i32 %t0, %b, !dbg !19
  %s.next = add i32 %s, %t1, !dbg !19
  %i.next = add nsw i32 %i, 1, !dbg !20
  %cmp = icmp slt i32 %i.next, %n, !dbg !20
  br i1 %cmp, label %loop, label %exit, !dbg !20

exit:
  ret i32 %s.next, !dbg !21
}

declare void @llvm.dbg.value(metadata, metadata, metadata)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "Haydn LLVM", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "bundle_line.c", directory: "/tmp")
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!7 = distinct !DISubprogram(name: "bundled_loop", scope: !1, file: !1, line: 1, type: !8, scopeLine: 1, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !12)
!8 = !DISubroutineType(types: !9)
!9 = !{!10, !11, !10, !10, !10}
!10 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!11 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !10, size: 32)
!12 = !{!13}
!13 = !DILocalVariable(name: "s", scope: !7, file: !1, line: 2, type: !10)
!16 = !DILocation(line: 3, column: 3, scope: !7)
!17 = !DILocation(line: 2, column: 7, scope: !7)
!18 = !DILocation(line: 4, column: 10, scope: !7)
!19 = !DILocation(line: 5, column: 7, scope: !7)
!20 = !DILocation(line: 6, column: 3, scope: !7)
!21 = !DILocation(line: 7, column: 3, scope: !7)

; LINE: .debug_line contents:
; LINE: min_inst_length: 2
; LINE: default_is_stmt: 1
; Golden two-byte min align: every line-table address is even (not forced
; ≡0 mod 12). Loop body lines 4/5 must appear with is_stmt.
; LINE-DAG: {{0x[0-9a-f]*[02468ace][[:space:]]+4[[:space:]]+}}{{.*}}is_stmt
; LINE-DAG: {{0x[0-9a-f]*[02468ace][[:space:]]+5[[:space:]]+}}{{.*}}is_stmt
; LINE: end_sequence

; DBG_VALUE survival through format-aware sched + FinalizeBundle.
; INFO: DW_TAG_variable
; INFO-NEXT: DW_AT_location
; INFO: DW_AT_name{{.*}}"s"
