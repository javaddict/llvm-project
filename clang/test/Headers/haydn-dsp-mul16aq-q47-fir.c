// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -S -ffreestanding -o - %s \
// RUN:   | FileCheck %s --check-prefix=ASM
//
// REQUIRES: haydn-registered-target
//
// REGRESSION TEST / CONTRACT LOCK: haydn_mul16aq (MUL16AQ, Q47 16x16 quad
// MAC) must stay callable from the NatureDSP KPI surface (<haydn_dsp.h>) and
// must lower to the mul16aq instruction.
//
// Bug being locked out: REPORT_bkfir_compiler_opt.md (BundleSim,
// benchmarks/naturedsp_kernels) §8 claimed MUL16AQ is "absent from the Haydn
// toolchain", which would block the 16x16 Q47 FIR (bkfir16x16, NatureDSP ILSS
// parity) on a missing opcode. Verified live 2026-08-21 at ddeeb67: ALL layers
// exist — golden instruction_type_index.json record (MUL16AQ rtd, rsd1, rsd2;
// Available [MAC0,MAC1]; rtd += sum of the four signed 16x16 lane products),
// IR intrinsic int_haydn_mul16aq (IntrinsicsHaydn.td), ISel
// selectAccMAC(MUL16AQ) (HaydnInstructionSelector.cpp), clang builtin mul16aq
// (BuiltinsHaydn.td, PublicName mul16aq), and generated E96 members
// MUL16AQ_E2_E0_MAC0_RR / MUL16AQ_E2_E1_MAC1_RR.
//
// Wrapper ownership: the public C wrapper haydn_mul16aq is NOT hand-written in
// haydn_dsp.h. It is published by the GENERATED haydn.h
// (clang-tblgen -gen-haydn-intrin-header; HaydnBuiltin defaults
// PublicEnabled=1), and haydn_dsp.h includes haydn.h, so the NatureDSP KPI
// surface already resolves it. Single-owner rule: haydn.h owns haydn_* native
// names; a duplicate static inline in haydn_dsp.h is a redefinition error
// (proven: clang rejects "redefinition of 'haydn_mul16aq'" against
// haydn.h). If this test ever fails with an implicit-declaration /
// undeclared 'haydn_mul16aq' error, the publication contract regressed —
// the TD record was dropped, PublicEnabled was flipped off, or the emitter
// stopped publishing it. Fix the publication, do not hand-copy the wrapper
// into haydn_dsp.h.
//
// Test design: one haydn_mul16aq performs the 4-lane 16x16 MAC of a tap
// quad, accumulated over a runtime-tap loop — the bkfir16x16 inner-op shape.
// IR run locks the intrinsic; ASM run locks the mnemonic.

#include <haydn_dsp.h>

static long long pack16x4(const short *p) {
  return (long long)(unsigned short)p[0] |
         ((long long)(unsigned short)p[1] << 16) |
         ((long long)(unsigned short)p[2] << 32) |
         ((long long)(unsigned short)p[3] << 48);
}

// IR-LABEL: @fir16x16_q47_acc
// IR: call i64 @llvm.haydn.mul16aq
//
// ASM-LABEL: fir16x16_q47_acc:
// ASM: mul16aq
long long fir16x16_q47_acc(long long acc, const short *h, const short *x,
                           int taps) {
  for (int k = 0; k + 4 <= taps; k += 4)
    acc = haydn_mul16aq(acc, pack16x4(x + k), pack16x4(h + k));
  return acc;
}
