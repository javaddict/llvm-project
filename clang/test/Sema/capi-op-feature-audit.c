// RUN: clang-tblgen -I %p/../../include -I %p/../../../llvm/include %p/../../include/clang/Basic/BuiltinsHaydn.td -gen-haydn-op-feature-audit -o %t.gen.c
// RUN: FileCheck %s --input-file=%t.gen.c --check-prefix=GEN
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu generic -fsyntax-only -verify=generic %t.gen.c
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -verify=full %t.gen.c
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -target-feature -agu -fsyntax-only -verify=noagu %t.gen.c
//
// CAPI-5 EXIT MET (2026-07-27) / G-CAPI-FEATURE continuous feature contract:
//   Generate exhaustive Features-gate Sema audits from BuiltinsHaydn.td
//   (OpenCL/NEON exhaustive + C3.1 haydn-capi-feature-gates.c sample peer).
//   Every PublicEnabled op with non-empty Features is called as
//   __builtin_haydn_* (SemaHaydn err_builtin_needs_feature home) — not UA
//   public switch wrappers / haydn_dsp.h.
//   Profiles: generic (same full ISA as haydn), full (haydn all five),
//   noagu (haydn -agu).
//   Fail closed: missing feature Sema case fails -verify.
//   No FormatID / slot / AltDesc in the public surface.
//   Residual product stress = BundleSim/ISS only (not a CAPI wave reopen).
//
// Companion: haydn-capi-feature-gates.c (hand sample) +
//            capi-op-imm-audit.c + capi-op-closure-matrix.c +
//            haydn-op-manifest-parity.td (Features column schema).

// GEN: Automatically generated from BuiltinsHaydn.td
// GEN: -gen-haydn-op-feature-audit
// GEN-DAG: generic-no-diagnostics
// GEN-DAG: full-no-diagnostics
// GEN: __builtin_haydn_
// GEN-DAG: feat_audit_x2add32
// GEN-DAG: feat_audit_ldw_cb_imm_pair
// GEN-DAG: feat_audit_ldw_brev_imm_pair
// GEN-DAG: feat_audit_brev32
// GEN-DAG: feat_audit_flar
// GEN-DAG: feat_audit_setcbr_begin
// GEN-DAG: feat_audit_wbarwua
// GEN-DAG: available on generic + full + noagu
// GEN-DAG: needs target feature agu
// GEN-NOT: FormatID
// GEN-NOT: AltDesc
// GEN-NOT: haydn_dsp
// GEN: Summary: feat_ops={{[0-9]+}} generic_fail=0 full_fail=0 noagu_fail={{[0-9]+}}
// GEN-SAME: by_feat: simd={{[0-9]+}} circular-buffer={{[0-9]+}} bit-reversed={{[0-9]+}} agu={{[0-9]+}}
