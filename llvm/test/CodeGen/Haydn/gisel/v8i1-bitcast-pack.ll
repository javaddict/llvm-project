; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s
;
; Role: semantic — -O2 SLP turns an 8-byte memcmp into
;   icmp <8 x i8> → freeze <8 x i1> → bitcast i8 → icmp eq -1
; G_ICMP scalarize rebuilds <8 x s1> via G_BUILD_VECTOR. Custom must pack
; those lanes (not return false and retry forever). This is
; embench nettle-sha256 verify_benchmark.

@hash = external global [8 x i8]
@buffer = external global [8 x i8]

define i32 @verify_8byte_eq() {
; CHECK-LABEL: verify_8byte_eq:
; CHECK:         jalr
  %a = load <8 x i8>, ptr @hash, align 4
  %b = load <8 x i8>, ptr @buffer, align 4
  %cmp = icmp eq <8 x i8> %a, %b
  %fr = freeze <8 x i1> %cmp
  %bits = bitcast <8 x i1> %fr to i8
  %all = icmp eq i8 %bits, -1
  %conv = zext i1 %all to i32
  ret i32 %conv
}
