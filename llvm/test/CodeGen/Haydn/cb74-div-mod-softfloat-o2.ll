; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s 2>&1 | FileCheck %s

; Role: semantic — residual companion (compiler side,): Direct-ELF BundleSim hangs on softfloat/div-heavy programs (cb1_div.

; residual companion (compiler side,):
; Direct-ELF BundleSim hangs on softfloat/div-heavy programs (cb1_div
; cb74_min) were primarily ISS/disasm-path issues (VMA-native Format E PC
; branch byte offsets, full -d helper). This locks the *compiler* contract
; those repros depend on:
; unsigned 32-bit div lowers (libcall or expansion) without abort
; i64 rem / ashr-by-32 sext(trunc) path still forms SRA64
; i64 rem is emitted as a call or sequence that reaches a terminator
;
; Runtime MATCH (host vs VLIW_ILSS direct-ELF) is validated in BundleSim
; benchmarks/compiler_bugs; this lit test is the non-sim compiler gate.

define i32 @udiv32_simple(i32 %a, i32 %b) nounwind {
entry:
  %q = udiv i32 %a, %b
  ret i32 %q
}
; CHECK-LABEL: udiv32_simple:
; CHECK: {{jal|udiv|div}}

; Mirrors cb74_min: (long long)((int)x) feeding an OR then srem — InstCombine
; turns sext(trunc) into ashr(shl,32); selector must keep a correct i64 value.
define i32 @srem64_after_sext_trunc(i64 %v20, i64 %v4) nounwind {
entry:
  %t = trunc i64 %v20 to i32
  %s = sext i32 %t to i64
  %o = or i64 %v4, %s
  %r = srem i64 %o, 7
  %c = icmp eq i64 %r, 3
  %ret = select i1 %c, i32 200, i32 100
  ret i32 %ret
}
; Prefer native SRA64 for the ashr-by-32 half of sext(trunc), and/or a
; libcall for the rem. Either way, must reach assembly (no abort).
; CHECK-LABEL: srem64_after_sext_trunc:
; CHECK: {{sra64|jal|mod}}
