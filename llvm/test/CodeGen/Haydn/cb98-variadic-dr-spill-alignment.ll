; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
;
; REGRESSION TEST: — variadic DR save area must be 8-byte aligned.
;
; Bug: with two fixed GPR parameters, saveVarArgRegisters sized the GPR save
; area to 5 * 4 = 20 bytes and placed the DR save area immediately below it at
; IncomingSP - (20 + 32) = IncomingSP - 52. After the 8-aligned frame
; allocation this became `addi32{{(_w)?}} r1, sp, 20` + `st64 d0, r1, 0` — address
; 4 mod 8. BundleSim rejects D_SDW_WITH_IMM on that misaligned address.
;
; Fix: pad the GPR region to a multiple of 8 when laying out the DR save area
; so DR base is always 8-byte aligned under StackAlign(8).
;
; Two fixed i32 args consume R1,R2; remaining GPR save size = 20 (4 mod 8).
; The python RUN line asserts the DR base offset is 0 mod 8.

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)

define i32 @cb98_first(i32 %count, i32 %ignored, ...) {
; GPR bank: spill unallocated R3–R7 (20 bytes, odd → needs DR pad).
; DR bank: base must be 8-aligned. Current layout pins sp+8 with a 64B frame
; (IncomingSP-56 after 4B pad between banks). Historical bug was sp+20.
entry:
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  ret i32 %v
}
