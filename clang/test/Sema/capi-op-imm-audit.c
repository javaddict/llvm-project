// RUN: clang-tblgen -I %p/../../include -I %p/../../../llvm/include %p/../../include/clang/Basic/BuiltinsHaydn.td -gen-haydn-op-imm-audit -o %t.gen.c
// RUN: FileCheck %s --input-file=%t.gen.c --check-prefix=GEN
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only -verify %t.gen.c
//
// C5.1 / G-SEMA-INTRIN continuous Imm contract:
//   Generate exhaustive ImmCheck non-ICE + range-negative Sema audits from
//   BuiltinsHaydn.td ImmChecks (NEON ImmCheck / OpenCL exhaustive-test peer).
//   Every PublicEnabled ImmChecked op is called as __builtin_haydn_* (Sema
//   ImmCheck home) — not UA public switch wrappers / haydn_dsp.h.
//   Fail closed: missing ImmCheck Sema case fails -verify.
//   No FormatID / slot / AltDesc in the public surface.
//
// Companion: haydn-immarg-const.c (hand sample) +
//            capi-op-closure-matrix.c (in-range C→object) +
//            haydn-op-manifest-parity.td (Imm list schema).

// GEN: Automatically generated from BuiltinsHaydn.td
// GEN: -gen-haydn-op-imm-audit
// GEN: __builtin_haydn_
// GEN-DAG: imm_audit_slli32_nc_a1
// GEN-DAG: imm_audit_slli32_hi_a1
// GEN-DAG: imm_audit_slli32_lo_a1
// GEN-DAG: imm_audit_srai64_hi_a1
// GEN-DAG: imm_audit_x4seli16_hi_a2
// GEN-DAG: imm_audit_ldw_cb_imm_pair_nc_a2
// GEN-DAG: imm_audit_ldw_cb_imm_pair_hi_a3
// GEN-DAG: imm_audit_wbarwua_nc_a0
// movei_l's ImmArg is argument 1, not 0: the database is `MOVEI_L rtd, imm32`
// with `rtd = {rtd[63:32], imm32}`, so rtd is read as well as written and takes
// the first slot. The probe name carries the argument index, which is why this
// moved when MOVEI was reshaped.
// GEN-DAG: imm_audit_movei_l_nc_a1
// GEN-NOT: FormatID
// GEN-NOT: AltDesc
// GEN-NOT: haydn_dsp
// GEN: Summary: imm_ops={{[0-9]+}} nonconst_tests={{[0-9]+}} range_tests={{[0-9]+}}
