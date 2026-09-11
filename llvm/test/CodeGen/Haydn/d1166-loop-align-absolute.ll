; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -function-sections -filetype=obj -verify-machineinstrs %s -o %t.o
; RUN: llvm-readobj -S --symbols %t.o | FileCheck %s --check-prefix=SEC
; RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
; RUN:   FileCheck %s --check-prefix=DIS
; REQUIRES: haydn-registered-target
;
; D1.166: llvm.loop.align 16 is an absolute address. A hot backedge with a
; skip-entry (MBP actually sets MBB alignment; gr19-loop-align-clears-p2align
; is vacuous because MBP rotates that header to function start) must:
;   * raise MF alignment so W70.2r pre-label fill is .p2align 4, not 2
;   * clear MBB metadata so the header itself has no residual .p2align 4
;   * land the loop body on a 16-byte object address (function-sections
;     symbol at 0 plus committed-packet pads).
; Peer: AIEMachineAlignment.cpp:370-424 padRegions; overlay is EncodedBytes
; idle packets + MF.ensureAlignment, not Min/Pref Align(16).

define i32 @hot_backedge(ptr nocapture readonly %p, i32 %n) {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %add, %loop ]
  %idx = getelementptr i32, ptr %p, i32 %i
  %val = load i32, ptr %idx, align 4
  %add = add i32 %sum, %val
  %inc = add i32 %i, 1
  %cmp = icmp eq i32 %inc, %n
  br i1 %cmp, label %exit, label %loop, !llvm.loop !0

exit:
  %r = phi i32 [ 0, %entry ], [ %add, %loop ]
  ret i32 %r
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.align", i32 16}

; MBP set the header alignment (not the function-entry skip). The named
; haydn-machine-alignment pass class is deleted (GR1.7 / GR2.10); stamped
; LBN closer owns padInternalMBBAlignment. Product asm/object pins below
; prove the absolute-address grid without -stop-before that pass.

; W70.2r pre-label fill follows the raised MF alignment. Header metadata
; is gone, so emitBasicBlockStart does not emit another .p2align 4.
; ASM: .p2align 4
; ASM-LABEL: hot_backedge:
; ASM-NOT: .p2align 4

; Function-sections: sh_addralign 16 is the absolute-address guarantee at
; offset 0 of the section.
; SEC: Name: .text.hot_backedge
; SEC: AddressAlignment: 16

; Loop body (running-sum add32 of the ZOL latch) sits on a 16-byte grid.
; Parcel addresses 0 mod 16 end in hex 0.
; DIS-LABEL: <hot_backedge>:
; DIS: {{^[ \t]+[0-9a-f]*0:}} {{.*}}add32{{.*}}r1, r1, r2
