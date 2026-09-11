; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -filetype=obj -o %t.o < %s
; RUN: llvm-readobj --symbols --relocations --expand-relocs %t.o | FileCheck %s

; Role: object — cross-object function calls must emit a relocation that references the extern symbol by name, NOT symbol index 0 (null).


;
; REGRESSION TEST: cross-object function calls must emit a relocation that
; references the extern symbol by name, NOT symbol index 0 (null).
;
; Bug history (M8-E2E):
; * / : HaydnELFObjectWriter::needsRelocateWithSymbol
; returned false unconditionally. Fixed by returning true to mirror RISC-V.
; But re-verification showed was NECESSARY BUT NOT SUFFICIENT: the
; extern symbol was STILL missing from.symtab entirely and the
; address-parcel reloc still referenced symbol index 0 ("-").
; * / : root cause found in the AsmPrinter. The Haydn
; AsmPrinter wraps EVERY instruction in a BUNDLE MCInst whose children are
; MCOperand::createInst operands. MCStreamer::emitInstruction only calls
; visitUsedExpr on the *direct* Expr operands of the MCInst it receives;
; it does NOT recurse into MCOperand::createInst children. So a child's
; Expr operand (e.g. the LUI/ADDI32 callee MCSymbolRefExpr) was never visited
; > the referenced extern MCSymbol was never registered with the
; MCAssembler (MCAssembler::registerSymbol) -> it never made it into
; MCAssembler::Symbols -> ELFWriter::computeSymbolTable never wrote it to
; symtab -> the relocation's R.Symbol->getIndex returned 0.
; RISC-V/ARM don't hit this because they emit calls as direct (non-bundled)
; MCInsts whose Expr operands ARE visited by the default streamer path.
; The textual-asm path (`-filetype=asm` -> `llvm-mc`) also worked, because
; the parser registers the symbol when it sees the bare name token.
;
; Fix : HaydnAsmPrinter gained registerSymbolicOperands, which walks
; an MCInst's operands (recursing into MCOperand::createInst children) and
; calls OutStreamer->visitUsedExpr on every Expr operand. It is invoked from
; emitWrappedInst and the multi-child bundle path in emitInstruction, BEFORE
; the BUNDLE is emitted to the streamer. This makes the undefined extern symbol
; land in.symtab and the reloc reference it by name/index.
;
; Note on RUN flags: --expand-relocs is a FORMAT flag for --relocations; it
; does not request relocations on its own. The original test used only
; symbols --expand-relocs, which silently never printed the relocation
; block, so the Relocation CHECKs were never exercised. The corrected RUN
; line adds --relocations so both blocks are printed (relocations before
; symbols).
;
; Test design: declare an extern function and call it from main. The
; resulting object MUST contain (a) an undefined global symbol "ext_func"
; in.symtab and (b) HI12/LO20 relocations whose Symbol field names
; "ext_func" (not "-", not index 0). If this regresses, the symbol
; vanishes and the relocation's Symbol reverts to "-" (index 0), breaking
; the link (the call resolves to address 0 at runtime). Short CallSImm20
; JAL is LLD cycle-neutral relax, not the compiler object form.

declare dso_local i32 @ext_func()

define dso_local i32 @main() {
entry:
  %r = call i32 @ext_func()
  ret i32 %r
}

; llvm-readobj prints the Relocations section before the Symbols section.
; CHECK:      Relocation {
; CHECK:        Type: R_HAYDN_HI12 (13)
; CHECK:        Symbol: ext_func
; CHECK:        Addend: 0x0
; CHECK:      Relocation {
; CHECK:        Type: R_HAYDN_LO20{{(_E1)?}}
; CHECK:        Symbol: ext_func
; CHECK:        Addend: 0x0
; CHECK:      Symbol {
; CHECK:        Name: ext_func
; CHECK:        Value: 0x0
; CHECK:        Binding: Global
; CHECK:        Type: None
; CHECK:        Section: Undefined (0x0)
