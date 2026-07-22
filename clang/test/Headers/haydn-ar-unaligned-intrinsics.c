// RUN: %clang_cc1 -triple haydn-unknown-elf -O2 -emit-llvm -ffreestanding -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple haydn-unknown-elf -O2 -S -ffreestanding -o - %s | FileCheck %s --check-prefix=ASM
//
// REQUIRES: haydn-registered-target
//
// E2E: C surface haydn_* AR unaligned builtins lower to llvm.haydn.* IR
// and emit native AR mnemonics in assembly.

#include <haydn.h>

// CHECK-LABEL: @c_pldwwua
// CHECK: call void @llvm.haydn.pldwwua
// ASM-LABEL: c_pldwwua
// ASM: pldwwua
void c_pldwwua(int ptr) {
  haydn_pldwwua(0, ptr);
}

// CHECK-LABEL: @c_flar
// CHECK: call void @llvm.haydn.flar
// ASM-LABEL: c_flar
// ASM: flar
void c_flar(void) {
  haydn_flar(0);
}

// CHECK-LABEL: @c_wbarwua
// CHECK: call void @llvm.haydn.wbarwua
// ASM-LABEL: c_wbarwua
// ASM: wbarwua
void c_wbarwua(int ptr) {
  haydn_wbarwua(0, ptr, 0);
}

// CHECK-LABEL: @c_lqhwua
// CHECK: call i64 @llvm.haydn.d.lqhwua.post
// ASM-LABEL: c_lqhwua
// ASM: d_lqhwua_post
int64_t c_lqhwua(int ptr, int stride) {
  return haydn_d_lqhwua_post(ptr, 0, stride, 0);
}

// CHECK-LABEL: @c_ltwua
// CHECK: call i64 @llvm.haydn.d.ltwua.post
// ASM-LABEL: c_ltwua
// ASM: d_ltwua_post
int64_t c_ltwua(int ptr, int stride) {
  return haydn_d_ltwua_post(ptr, 1, stride, 0);
}

// CHECK-LABEL: @c_sqhwua
// CHECK: call void @llvm.haydn.d.sqhwua.post
// ASM-LABEL: c_sqhwua
// ASM: d_sqhwua_post
void c_sqhwua(int64_t data, int ptr, int stride) {
  haydn_d_sqhwua_post(data, ptr, 0, stride, 0);
}

// CHECK-LABEL: @c_stwua
// CHECK: call void @llvm.haydn.d.stwua.post
// ASM-LABEL: c_stwua
// ASM: d_stwua_post
void c_stwua(int64_t data, int ptr, int stride) {
  haydn_d_stwua_post(data, ptr, 0, stride, 0);
}

// CHECK-LABEL: @c_stream_load
// CHECK: call void @llvm.haydn.pldwwua
// CHECK: call i64 @llvm.haydn.d.lqhwua.post
// CHECK: call void @llvm.haydn.flar
// ASM-LABEL: c_stream_load
// ASM: pldwwua
// ASM: d_lqhwua_post
// ASM: flar
int64_t c_stream_load(int base, int ptr, int stride) {
  haydn_pldwwua(0, base);
  int64_t v = haydn_d_lqhwua_post(ptr, 0, stride, 0);
  haydn_flar(0);
  return v;
}

// CHECK-LABEL: @c_stream_ip
// CHECK: call i64 @llvm.haydn.d.lqhwua.post
// Streaming helper updates the C cursor after the HW op.
int64_t c_stream_ip(int *pptr, int stride) {
  return haydn_d_lqhwua_post_ip(pptr, 0, stride, 0);
}
