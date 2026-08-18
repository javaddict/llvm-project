; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
;
; 64-bit SIMD mem must honour MMO alignment. ISel splits an under-8-aligned
; 64-bit access into D_SW_L/D_SW_H (or LD32) halves, which are 4-byte ops, so
; type-only vector legality let a byte-aligned <8 x i8> become two 4-byte
; accesses -> guest MEMORY_FAULT "guest access is misaligned".
;
; Not synthetic: llvm-libc has no Haydn memcpy/memset, so it uses
; generic/byte_per_byte.h, and the loop vectorizer turns those byte loops into
; honest `<8 x i8> ... align 1` mem ops. That is how memcpy, memset, strcpy,
; strncpy and printf_core all faulted (27 of 29 gcc-c-torture failures).
;
; Product (E96 Option C): MemDesc AlignInBits=32 keeps align-4 vectors legal;
; align <4 residual must lower to element-sized ops (scalarize or bitcast→s64
; lower) — never emit underaligned dual-half d_sw_/ld32. Pin the guest-safe
; ladder, not one legalizer recipe.

; CHECK-LABEL: st_v8i8_align1:
; CHECK-NOT: d_sw_
; CHECK-NOT: st64
; CHECK: st8
define void @st_v8i8_align1(ptr %p, <8 x i8> %v) nounwind {
  store <8 x i8> %v, ptr %p, align 1
  ret void
}

; CHECK-LABEL: st_v8i8_align2:
; CHECK-NOT: d_sw_
; CHECK-NOT: st64
; CHECK: st8
define void @st_v8i8_align2(ptr %p, <8 x i8> %v) nounwind {
  store <8 x i8> %v, ptr %p, align 2
  ret void
}

; align 4 keeps 4-byte ops on 4-byte aligned memory — ST32 halves or
; D_SW_L/H are both guest-safe. Do not require one recipe.
; CHECK-LABEL: st_v8i8_align4:
; CHECK: {{d_sw_|st32}}
define void @st_v8i8_align4(ptr %p, <8 x i8> %v) nounwind {
  store <8 x i8> %v, ptr %p, align 4
  ret void
}

; Natural alignment still gets the single 64-bit store.
; CHECK-LABEL: st_v8i8_align8:
; CHECK: st64
define void @st_v8i8_align8(ptr %p, <8 x i8> %v) nounwind {
  store <8 x i8> %v, ptr %p, align 8
  ret void
}

; CHECK-LABEL: st_v4i16_align1:
; CHECK-NOT: d_sw_
; CHECK: st8
define void @st_v4i16_align1(ptr %p, <4 x i16> %v) nounwind {
  store <4 x i16> %v, ptr %p, align 1
  ret void
}

; CHECK-LABEL: st_v2i32_align1:
; CHECK-NOT: d_sw_
; CHECK: st8
define void @st_v2i32_align1(ptr %p, <2 x i32> %v) nounwind {
  store <2 x i32> %v, ptr %p, align 1
  ret void
}

; Load side: byte-aligned vector load must not use LD32/LD64 on the pointer.
; CHECK-LABEL: ld_v8i8_align1:
; CHECK: ldu8
define <8 x i8> @ld_v8i8_align1(ptr %p) nounwind {
  %r = load <8 x i8>, ptr %p, align 1
  ret <8 x i8> %r
}

; CHECK-LABEL: ld_v8i8_align8:
; CHECK: ld64
define <8 x i8> @ld_v8i8_align8(ptr %p) nounwind {
  %r = load <8 x i8>, ptr %p, align 8
  ret <8 x i8> %r
}
