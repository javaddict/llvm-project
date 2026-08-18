// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -ffreestanding -fsyntax-only %s
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -mllvm -enable-misched=false \
// RUN:   -mllvm -enable-post-misched=false -ffreestanding \
// RUN:   -emit-llvm -S -o - %s | FileCheck %s --check-prefix=IR
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O0 -mllvm -enable-misched=false \
// RUN:   -mllvm -enable-post-misched=false -ffreestanding \
// RUN:   -c -o %t.o %s
// RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=OBJ
// RUN: not clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding \
// RUN:   -fsyntax-only -DTEST_X2CMUL_STRICT %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CMUL
// RUN: not clang -target haydn-unknown-elf -ffreestanding -fsyntax-only %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=GENERIC
//
// Sat/round family host oracles + fail-closed X2CMUL / default-CPU trap.
// Empty output fails. Default fail-closed (no __HAYDN_ALLOW_INEXACT_AE).

#include <haydn_dsp.h>

_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");
_Static_assert(sizeof(ae_int64) == 8, "no extra accumulator guard bits");
#if defined(__HAYDN_ACC_GUARD_BITS)
_Static_assert(0, "do not invent extra HiFi accumulator guard bits");
#endif
#if defined(AE_MULAAAAQ16)
_Static_assert(0, "AE_MULAAAAQ16 must be undefined under default strict");
#endif

#ifndef TEST_X2CMUL_STRICT
// SAT32(+2^32) = INT32_MAX. IR-LABEL: @tdsp5_sat32_pos
// IR: ret i32 2147483647
// OBJ-LABEL: <tdsp5_sat32_pos>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
int tdsp5_sat32_pos(void) { return haydn_satsr64(0x100000000LL, 0); }

// SAT32(-2^32) = INT32_MIN. IR-LABEL: @tdsp5_sat32_neg
// IR: ret i32 -2147483648
// OBJ-LABEL: <tdsp5_sat32_neg>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
int tdsp5_sat32_neg(void) { return haydn_satsr64(-0x100000000LL, 0); }

// INT32_MIN^2 = 2^62, then SAT32 -> INT32_MAX.
// IR-LABEL: @tdsp5_sat32_intmin_sq
// IR: ret i32 2147483647
// OBJ-LABEL: <tdsp5_sat32_intmin_sq>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
int tdsp5_sat32_intmin_sq(void) {
  return haydn_satsr64((int64_t)(int32_t)0x80000000 * (int64_t)(int32_t)0x80000000,
                       0);
}

// SLAA64S 1<<1 = 2. IR-LABEL: @tdsp5_slaa64s_one
// IR: ret i64 2
// OBJ-LABEL: <tdsp5_slaa64s_one>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
ae_int64 tdsp5_slaa64s_one(void) { return AE_SLAA64S((ae_int64)1, 1); }

// 0x4000... << 1 saturates to INT64_MAX.
// IR-LABEL: @tdsp5_slaa64s_sat_max
// IR: ret i64 9223372036854775807
// OBJ-LABEL: <tdsp5_slaa64s_sat_max>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
ae_int64 tdsp5_slaa64s_sat_max(void) {
  return AE_SLAA64S((ae_int64)0x4000000000000000LL, 1);
}

// Host *r model: shift 0 identity. IR-LABEL: @tdsp5_srai64r_id
// IR: ret i64 7
// OBJ-LABEL: <tdsp5_srai64r_id>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
int64_t tdsp5_srai64r_id(void) { return haydn_ae_asr_round64_host(7, 0); }

// Host *r tie: (2 + 1<<(2-1)) >> 2 = 1. IR-LABEL: @tdsp5_srai64r_tie
// IR: ret i64 1
// OBJ-LABEL: <tdsp5_srai64r_tie>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
int64_t tdsp5_srai64r_tie(void) { return haydn_ae_asr_round64_host(2, 2); }

// Host *r negative: (-3 + 2) >> 2 = -1. IR-LABEL: @tdsp5_srai64r_neg
// IR: ret i64 -1
// OBJ-LABEL: <tdsp5_srai64r_neg>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
int64_t tdsp5_srai64r_neg(void) { return haydn_ae_asr_round64_host(-3, 2); }

// Native *r mapping stays a real member (does not constant-fold at IR).
// IR-LABEL: @tdsp5_srai64r_native
// IR: call i64 @llvm.haydn.srai64r(i64 2, i32 2)
// OBJ-LABEL: <tdsp5_srai64r_native>:
// OBJ: {{srai64r|sra64|SRA}}
int64_t tdsp5_srai64r_native(void) { return haydn_srai64r(2, 2); }

// Dual-32 *r mapping. IR-LABEL: @tdsp5_sraa32rs_tie
// IR: call {{.*}}@llvm.haydn.x2sra32r
// OBJ-LABEL: <tdsp5_sraa32rs_tie>:
// OBJ: {{x2sra32r|sra32|SRA}}
ae_int64 tdsp5_sraa32rs_tie(void) {
  ae_int32x2 a = {2, 2};
  return __AE_TO_I64(AE_SRAA32RS(a, 2));
}

// packsr32 is a header composite (SRA64R then narrow), not an IR intrinsic.
// Host *r model of that compose: (2 + 1<<(2-1)) >> 2 = 1.
// IR-LABEL: @tdsp5_packsr32_host
// IR: ret i32 1
// OBJ-LABEL: <tdsp5_packsr32_host>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
int tdsp5_packsr32_host(void) { return (int)haydn_ae_asr_round64_host(2, 2); }

// Dual-32 sat-left 1<<1 = 2 on both lanes (0x0000000200000002).
// IR-LABEL: @tdsp5_slaa32s_one
// IR: ret i64 8589934594
// OBJ-LABEL: <tdsp5_slaa32s_one>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
ae_int64 tdsp5_slaa32s_one(void) {
  ae_int32x2 a = {1, 1};
  return __AE_TO_I64(AE_SLAA32S(a, 1));
}

// 0x40000000 << 1 saturates to INT32_MAX on both lanes.
// IR-LABEL: @tdsp5_slaa32s_sat
// IR: ret i64 9223372034707292159
// OBJ-LABEL: <tdsp5_slaa32s_sat>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
ae_int64 tdsp5_slaa32s_sat(void) {
  ae_int32x2 a = {0x40000000, 0x40000000};
  return __AE_TO_I64(AE_SLAA32S(a, 1));
}

// Quad-16 sat-left 1<<1 = 2 on every lane (0x0002000200020002).
// IR-LABEL: @tdsp5_slaa16s_one
// IR: ret i64 562958543486978
// OBJ-LABEL: <tdsp5_slaa16s_one>:
// OBJ: {{addi32|lui|ori|movi|jalr}}
ae_int64 tdsp5_slaa16s_one(void) {
  ae_int16x4 a = {1, 1, 1, 1};
  return __AE_TO_I64(AE_SLAA16S(a, 1));
}
#endif

#ifdef TEST_X2CMUL_STRICT
// CMUL: __haydn_ae_unsupported_AE_MULC32X16_H
ae_int32x2 tdsp5_mulc32x16_h(ae_int32x2 a, ae_int32x2 b) {
  return AE_MULC32X16_H(a, b);
}
// CMUL: silent-wrong map removed): AE_CMUL32_F2
void tdsp5_cmul32_f2(ae_int32x2 *d0, ae_int32x2 *d1, ae_int32x2 a,
                     ae_int32x2 b) {
  AE_CMUL32_F2(*d0, *d1, a, b);
}
#endif

// GENERIC: needs target feature simd
