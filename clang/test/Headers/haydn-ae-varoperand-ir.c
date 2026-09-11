// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -O2 -emit-llvm \
// RUN:   -ffreestanding -o - %s | FileCheck %s --check-prefix=IR
//
// REQUIRES: haydn-registered-target
//
// IR layer of the 2026-09-02 adversarial-operand wave (compile-layer peer:
// haydn-ae-varoperand-compile.c; XC history: haydn_dsp_xc.c).
//
// Laws pinned (single reg law):
//   * AE_L16X4_XC with a RUNTIME stride lowers to llvm.haydn.ldw.cb.reg
//     (D_LDW_CB_REG), cbr_sel ICE 0 — never the imm-only
//     llvm.haydn.ldw.cb.imm (hard Sema error on non-ICE stride).
//   * CONSTANT strides take the SAME reg path: D_LDW_CB_REG takes rs2
//     RAW BYTES (golden: effective_addr = rs1 + rs2, no <<3), so literal
//     16 reaches the reg intrinsic as a constant operand i32 16.
//   * AE_S16X4_XC variable stride lowers to llvm.haydn.sdw.cb.reg.
//   * AE_SHORTSWAP lowers llvm.haydn.x4seli16(a, a, i32 0) — golden
//     sel=0 halves swap; 0xB4 (180) is outside uimm4 [0,15].
//   * AE_MOVAD16_N are lane extracts: extractelement <4 x i16>, i64 N
//     (Clang emits the lane index as i64).

#include <haydn_dsp.h>

// IR-LABEL: @xc_var_stride_16x4
// Variable byte stride must reach D_LDW_CB_REG as a GPR value (raw bytes,
// no element scaling); cbr_sel stays ICE 0.
// IR: call {{.*}}@llvm.haydn.ldw.cb.reg({{[^)]*}}i32 0
// IR-NOT: call {{.*}}@llvm.haydn.ldw.cb.imm
ae_int16x4 *xc_var_stride_16x4(ae_int16x4 *p, int stride) {
  ae_int16x4 d = {0};
  AE_L16X4_XC(d, p, stride);
  (void)d;
  return p;
}

// IR-LABEL: @xc_const_stride_16x4_single_law
// Constant stride takes the SAME reg path (single law): D_LDW_CB_REG takes
// RAW BYTES (golden: effective_addr = rs1 + rs2, no <<3), so literal 16
// reaches the reg intrinsic as a constant operand i32 16.
// IR: call {{.*}}@llvm.haydn.ldw.cb.reg({{[^)]*}}i32 0, i32 16
// IR-NOT: call {{.*}}@llvm.haydn.ldw.cb.imm
ae_int16x4 *xc_const_stride_16x4_single_law(ae_int16x4 *p) {
  ae_int16x4 d = {0};
  AE_L16X4_XC(d, p, 16);
  (void)d;
  return p;
}

// IR-LABEL: @xc_var_stride_store_16x4
// Store peer: variable stride → D_SDW_CB_REG (cbr_sel ICE 1).
// IR: call {{.*}}@llvm.haydn.sdw.cb.reg({{[^)]*}}i32 1
// IR-NOT: call {{.*}}@llvm.haydn.sdw.cb.imm
ae_int16x4 *xc_var_stride_store_16x4(ae_int16x4 *p, int stride) {
  ae_int16x4 d = {0};
  AE_S16X4_XC(d, p, stride, 1);
  return p;
}

// IR-LABEL: @shortswap_sel0
// AE_SHORTSWAP = x4seli16(a, a, sel 0): golden halves swap. The broken
// 0xB4 body was a Sema range error on every use; this pin also fails if
// the imm regresses to any value that cannot encode in uimm4.
// IR: call {{.*}}@llvm.haydn.x4seli16({{[^)]*}}i32 0)
ae_int16x4 shortswap_sel0(ae_int16x4 a) {
  return AE_SHORTSWAP(a);
}

// IR-LABEL: @movad16_lane0
// AE_MOVAD16_N(a) = lane N of ae_int16x4 (scalar extract; bexp idiom).
// IR: extractelement <4 x i16> {{[^,]+}}, i64 0
int movad16_lane0(ae_int16x4 a) { return AE_MOVAD16_0(a); }

// IR-LABEL: @movad16_lane1
// IR: extractelement <4 x i16> {{[^,]+}}, i64 1
int movad16_lane1(ae_int16x4 a) { return AE_MOVAD16_1(a); }

// IR-LABEL: @movad16_lane2
// IR: extractelement <4 x i16> {{[^,]+}}, i64 2
int movad16_lane2(ae_int16x4 a) { return AE_MOVAD16_2(a); }

// IR-LABEL: @movad16_lane3
// IR: extractelement <4 x i16> {{[^,]+}}, i64 3
int movad16_lane3(ae_int16x4 a) { return AE_MOVAD16_3(a); }
