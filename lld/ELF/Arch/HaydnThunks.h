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
// `ctx.arg.emachine == EM_HAYDN`. Selects the Haydn long-branch thunk for
// out-of-range BranchSImm16 / CallSImm20 / WIDE_BranchSImm12[_RI] /
// WIDE_CallSImm20 relocations (must match Haydn::needsThunk).
// Veneer geometry is 3 × production EncodedBytes (registry), Align-4.
std::unique_ptr<Thunk> addThunkHaydn(Ctx &ctx, const InputSection &isec,
                                     Relocation &rel, Symbol &s);

} // namespace lld::elf

#endif // LLD_ELF_ARCH_HAYDN_THUNKS_H
