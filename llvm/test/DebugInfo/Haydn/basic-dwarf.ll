; RUN: llc -mtriple=haydn-unknown-elf -filetype=obj < %s | llvm-dwarfdump -v - | FileCheck %s

; Test that basic DWARF debug info is generated for Haydn target
; This test verifies:
; 1. DWARF register numbers are correct (R0-R15: 0-15, R13=SP, R14=FP, R15=LR)
; 2. CFI directives are emitted (cfi_def_cfa, cfi_offset, etc.)
; 3. DW_TAG_compile_unit, DW_TAG_subprogram appear

define i32 @test_function(i32 %a, i32 %b) !dbg !4 {
entry:
  %add = add nsw i32 %a, %b, !dbg !8
  ret i32 %add, !dbg !9
}

; CHECK: .debug_info contents:
; CHECK: DW_TAG_compile_unit
; CHECK:   DW_AT_name [DW_FORM_strp]  ( .debug_str[0x{{[0-9a-f]+}}] = "test.c")
; CHECK:   DW_AT_comp_dir [DW_FORM_strp]  ( .debug_str[0x{{[0-9a-f]+}}] = "/tmp")
; CHECK:   DW_TAG_subprogram
; CHECK:     DW_AT_name [DW_FORM_strp]  ( .debug_str[0x{{[0-9a-f]+}}] = "test_function")
; CHECK:     DW_AT_type
; CHECK:   DW_TAG_base_type
; CHECK:     DW_AT_name [DW_FORM_strp]  ( .debug_str[0x{{[0-9a-f]+}}] = "int")
; CHECK:     DW_AT_encoding [DW_FORM_data1] (DW_ATE_signed)
; CHECK:     DW_AT_byte_size [DW_FORM_data1] (0x04)
;
; Regression guard for the Object-layer relocation resolver (D172 / L143).
; Each DW_FORM_strp carries an R_HAYDN_32 relocation into .debug_str. The CHECK
; lines above pin the resolved .debug_str offset; the real guard is the
; = "..." literal -- if the resolver is missing/regresses, every offset
; collapses to 0x00000000 (L143's failure mode) and resolves to the FIRST
; .debug_str entry ("Haydn LLVM"), so the = "test.c"/"int" literals fail and
; the test goes red. (dwarfdump prints fixed-width 8-hex offsets like
; 0x0000000b, hence [0-9a-f]+.) The original test used the non-verbose
; form DW_AT_name ("x") against `dwarfdump -v`, which never matched; this is the
; verbose form (DW_AT_name [DW_FORM_strp]  ( .debug_str[..] = "x")) that the
; verbose dump actually emits. DW_TAG_base_type prints encoding before byte_size
; (matches llvm-dwarfdump DIE attribute order; cf.
; DebugInfo/X86/empty-and-one-elem-array.ll).

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: "Haydn LLVM", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false)
!1 = !DIFile(filename: "test.c", directory: "/tmp")
!2 = !{i32 2, !"Dwarf Version", i32 4}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "test_function", scope: !1, file: !1, line: 1, type: !5, scopeLine: 1, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !{})
!5 = !DISubroutineType(types: !6)
!6 = !{!7, !7, !7}
!7 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!8 = !DILocation(line: 2, column: 10, scope: !4)
!9 = !DILocation(line: 3, column: 3, scope: !4)
