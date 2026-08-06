// RUN: clang-tblgen -I %p/../../../include -I %p/../../../../llvm/include %p/../../../include/clang/Basic/BuiltinsHaydn.td -gen-haydn-op-closure-probe -o %t.gen.c
// RUN: FileCheck %s --input-file=%t.gen.c --check-prefix=GEN
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -emit-obj -O0 -o %t.o0.o %t.gen.c
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -emit-obj -O2 -o %t.o2.o %t.gen.c
// REQUIRES: haydn-registered-target
//
// CAPI-5 EXIT MET (2026-07-27) / G-CAPI-CLOSURE continuous-closure gate:
//   Generate an exhaustive PublicEnabled C probe from BuiltinsHaydn.td
//   (OpenCL exhaustive-test peer) and compile C → object at -O0 and -O2.
//   Every public haydn_* is called with type-driven dummies + in-range ImmArg
//   ICE. Fail closed: fix ISel or unpublish any miss (prefer implement).
//   No FormatID / slot / AltDesc in the public surface.
//   Residual product stress = BundleSim/ISS only (not a CAPI wave reopen).
//
// Companion: capi-public-closure-probe.c (family samples) +
//            haydn-op-manifest-parity.td (schema contract).

// GEN: Automatically generated from BuiltinsHaydn.td
// GEN: -gen-haydn-op-closure-probe
// GEN: #include <haydn.h>
// GEN-DAG: void capi_op_closure_abs32s(
// GEN-DAG: void capi_op_closure_arctan(
// GEN-DAG: haydn_arctan(
// GEN-DAG: void capi_op_closure_ldw_cb_imm(
// GEN-DAG: haydn_ldw_cb_imm(
// GEN-DAG: void capi_op_closure_x2cmula32(
// GEN-DAG: void capi_op_closure_isqrt(
// GEN-DAG: void capi_op_closure_d_lw_with_imm(
// GEN-DAG: void capi_op_closure_x2cmplt32(
// GEN-NOT: FormatID
// GEN-NOT: AltDesc
// GEN: Summary: probed={{[0-9]+}}
