// RUN: clang-tblgen -I %p/../../include -I %p/../../../llvm/include \
// RUN:   %p/../../include/clang/Basic/BuiltinsHaydn.td \
// RUN:   -gen-haydn-intrin-header -o - | FileCheck %s
//
// T-DSP11 / T-DSP14 surface-truth pin on the generated haydn.h comments
// (BuiltinsHaydn.td Semantics). Does not edit haydn_dsp.h.
// Unique substrings so X2CMUL32 does not also match X2CMUL32S/_F2.

// CHECK-DAG: ISA: MOVDA32X2 — {{.*}}DR64PackFI memory round-trip
// CHECK-DAG: ISA: MOVDA32 — compiler emulation{{.*}}No native GPR->DR lane-insert (ISA-63)
// CHECK-DAG: ISA: X2CMUL32 — performs a complex multiplication of two 32-bit{{.*}}SOURCE-REVALIDATE (T-DSP14)
// CHECK-DAG: ISA: X2CMUL32S — performs a saturated complex multiplication using Format-1{{.*}}SOURCE-REVALIDATE (T-DSP14)
// CHECK-DAG: ISA: X2CMUL32_F2 — {{.*}}SOURCE-REVALIDATE (T-DSP14)
// CHECK-DAG: ISA: X2CMUL32S_F2 — {{.*}}SOURCE-REVALIDATE (T-DSP14)
