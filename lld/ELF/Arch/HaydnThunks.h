//===- HaydnThunks.h ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn long-branch thunk factory. Declared in a Haydn-owned header so the
// shared `lld/ELF/Thunks.cpp` only needs a minimal `case EM_HAYDN:` dispatch
// that forwards here — keeping all Haydn-specific thunk policy out of the
// shared upstream file (HC#0).
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_ARCH_HAYDN_THUNKS_H
#define LLD_ELF_ARCH_HAYDN_THUNKS_H

#include "lld/Common/LLVM.h"

namespace lld::elf {

class Ctx;
class InputSection;
class Relocation;
class Symbol;
class Thunk;

// Factory invoked from the shared `elf::addThunk()` dispatcher when
// `ctx.arg.emachine == EM_HAYDN`. FAIL CLOSED (D1.57): the Haydn veneer ABI
// is not approved (ISA-70), so every thunk-eligible relocation — the base
// BranchSImm16 / CallSImm20 / WIDE_BranchSImm12[_RI] / WIDE_CallSImm20
// kinds and the D1.58 E3-qualified rows that Haydn::needsThunk arms — is
// rejected with an explicit ABI-gap diagnostic. No R0-writing thunk bytes
// are emitted; no long-branch thunk class exists.
std::unique_ptr<Thunk> addThunkHaydn(Ctx &ctx, const InputSection &isec,
                                     Relocation &rel, Symbol &s);

} // namespace lld::elf

#endif // LLD_ELF_ARCH_HAYDN_THUNKS_H
