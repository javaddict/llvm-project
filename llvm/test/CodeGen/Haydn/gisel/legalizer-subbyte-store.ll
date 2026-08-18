; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -verify-machineinstrs < %s -o - 2>&1 | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s -o - 2>&1 | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -stop-after=legalizer -verify-machineinstrs < %s -o - \
; RUN:     | FileCheck %s --check-prefix=LEG
;
; Sub-byte G_STORE mem (`store i1`, MemoryTy s1) must lower to a byte store.
; The value/mem mismatch customIf in HaydnLegalizerInfo only owns whole-byte
; mem: for s1 it used to livelock (minScalar widened the value to s8, custom
; truncated it back to s1 and rewrote the MMO to s1, forever — no diagnostic,
; unbounded memory). InstCombine forms `store i1` from plain `bool` flags at
; -O2, so this reached the freestanding BSP, not just synthetic IR.

; CHECK-LABEL: store_i1_const:
; CHECK: st8
define void @store_i1_const(ptr %p) nounwind {
  store i1 true, ptr %p, align 1
  ret void
}

; CHECK-LABEL: store_i1_dynamic:
; CHECK: st8
define void @store_i1_dynamic(ptr %p, i32 %v) nounwind {
  %b = icmp ne i32 %v, 0
  store i1 %b, ptr %p, align 1
  ret void
}

; Round-trip through an i1 alloca (the -O0 `%retval` shape clang emits for a
; function returning bool with early returns).
; CHECK-LABEL: store_i1_alloca_roundtrip:
; CHECK: st8
define zeroext i1 @store_i1_alloca_roundtrip(i32 %v) nounwind {
entry:
  %retval = alloca i1, align 1
  %c = icmp sgt i32 %v, 0
  br i1 %c, label %pos, label %neg

pos:
  store i1 true, ptr %retval, align 1
  br label %out

neg:
  store i1 false, ptr %retval, align 1
  br label %out

out:
  %r = load i1, ptr %retval, align 1
  ret i1 %r
}

; Plain byte store: value width == mem width, so this is legal straight out of
; legalForTypesWithMemDesc and never reaches the customIf. It only guards that
; the sub-byte change left ordinary byte stores alone. (`G_STORE s32 :: (store
; s64)`, the shape the customIf actually owns, cannot be written in IR — a
; store's MMO width always equals its value width there — it only appears when
; the legalizer itself rewrites a wider store.)
; CHECK-LABEL: store_i8_mem:
; CHECK: st8
define void @store_i8_mem(ptr %p, i32 %v) nounwind {
  %t = trunc i32 %v to i8
  store i8 %t, ptr %p, align 1
  ret void
}

; Non-pow2 whole-byte mem must keep reaching lowerIfMemSizeNotByteSizePow2():
; s40 splits into a 32-bit store plus an s8 store at +4. Pin the legalizer
; MMOs; ISel recipe for the s8 half is not this layer.
; LEG-LABEL: name: store_i40_mem
; LEG: G_STORE {{.*}}(store (s32)
; LEG: G_STORE {{.*}}(store (s8) {{.*}}+ 4
; CHECK-LABEL: store_i40_mem:
define void @store_i40_mem(ptr %p, i40 %v) nounwind {
  store i40 %v, ptr %p, align 8
  ret void
}
