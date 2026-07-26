// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -ffreestanding -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -S -ffreestanding -o - %s | FileCheck %s --check-prefix=ASM
//
// REQUIRES: haydn-registered-target
//
// HiFi contract: AE_LA32X2F24_IP loads then advances ptr by one vector.
// After AR rewire this is D_LTWUA_POST (llvm.haydn.d.ltwua.post), not a plain
// IR load — without the advance, LICM freezes the stream (poly/alog P5 class).
//
// CHECK-LABEL: @stream_postinc
// CHECK: call i64 @llvm.haydn.d.ltwua.post
// CHECK: getelementptr
// CHECK: call i64 @llvm.haydn.d.ltwua.post
// ASM-LABEL: stream_postinc
// ASM: d_ltwua_post
// ASM: d_ltwua_post

#include <haydn_dsp.h>

void stream_postinc(ae_f24x2 *p, ae_f24x2 *out) {
  ae_valign a = AE_LA64_PP(p);
  ae_f24x2 v0, v1;
  AE_LA32X2F24_IP(v0, a, p);
  AE_LA32X2F24_IP(v1, a, p);
  out[0] = v0;
  out[1] = v1;
}
