; RUN: llc -mtriple=haydn-unknown-elf -filetype=obj < %s | llvm-dwarfdump - | FileCheck %s

; CHECK: DW_TAG_compile_unit
; CHECK:   DW_AT_name
; CHECK:   DW_AT_comp_dir
; CHECK:   DW_TAG_subprogram
; CHECK:     DW_AT_name ("test")
; CHECK:     DW_AT_type
; CHECK:   DW_TAG_base_type
; CHECK:     DW_AT_name ("int")
; CHECK:     DW_AT_encoding (DW_ATE_signed)
; CHECK:     DW_AT_byte_size (0x04)

define i32 @test(i32 %a) !dbg !4 {
entry:
  %add = add nsw i32 %a, 1, !dbg !8
  ret i32 %add, !dbg !9
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: "Haydn LLVM", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false)
!1 = !DIFile(filename: "test.c", directory: "/tmp")
!2 = !{i32 2, !"Dwarf Version", i32 4}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "test", scope: !1, file: !1, line: 1, type: !5, scopeLine: 1, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !{})
!5 = !DISubroutineType(types: !6)
!6 = !{!7, !7}
!7 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!8 = !DILocation(line: 2, column: 10, scope: !4)
!9 = !DILocation(line: 3, column: 3, scope: !4)
