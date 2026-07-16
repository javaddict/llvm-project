//===-- HaydnInstrInfoC.td - Compressed Instructions (RETIRED) -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// All C_* 16-bit compressed instruction shells deleted.
// Bundle128-only path: no 16-bit C parcels on the live emit/decode path
// FLEX encode-only forms C_ADD32_S0 / C_ADDI32_S0 remain
// in HaydnFormatsALU32.td. AsmBackend C_* relaxation stubs were removed with
// these defs. This file is kept as an empty include so HaydnGeneric.td does
// not churn; it may be un-included in a follow-on cleanup.
//
// Historical C_* islands are not re-archived here (see earlier git history
// and compress*.ll UNSUPPORTED tests under llvm/test/CodeGen/Haydn).
//
//===----------------------------------------------------------------------===//

// Intentionally empty — no live C_* instruction defs.
