//===- HaydnThunks.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// Haydn long-branch / long-call linker veneers: FAIL CLOSED (D1.57).
//
// The retired HaydnLongThunk materialized
//
//   LUI    R0, hi12(target + addend)
//   ADDI32 R0, R0, lo20(...)
//   JALR   R0, R0, 0          // PC = target; R0 = link (never falls through)
//
// into soft-zero R0. JALR writes PC_next into R0, so a mid-function far
// branch could arrive with R0 dirty (R0 == link, not 0) and later control
// flow could jump through a garbage address — linker wrong-code. That
// template (and its parcel writers) is deleted: branch/call thunking is
// rejected with an explicit diagnostic until an ABI-approved
// non-clobbering veneer template and its discard/link destination exist.
// The veneer ABI design is owned by ISA-70 (linker-veneer transfer
// contract) with the exhaustive relocation-to-thunk classification in
// D1.58; neither is part of this file.
//
// `needsThunks` stays armed in Haydn.cpp so out-of-range branch/call sites
// (Haydn::needsThunk) reach this factory and get the named ABI-gap error
// instead of a generic relocation-range failure; in-range sites never get
// here and link unchanged. No thunk islands are pre-created
// (getThunkSectionSpacing keeps the 0 default) because no thunk can be
// produced.
//
// This factory is expansion (veneer), not relaxation. RISC-V relaxCall
// may delete AUIPC and shrink a call to jal. Haydn must not: a committed
// Format E packet is one cycle, and dropping LUI/ADDI packets would
// change the scheduled cycle grid and can delete siblings packed with
// those members. In-range CallSImm20 rewrite, when added, shortens the
// JALR member in place and NOPs vacated address children; it does not
// live here and must not reduce cycle count.
//
//===----------------------------------------------------------------------===//

#include "HaydnThunks.h"

#include "InputSection.h"
#include "Symbols.h"
#include "Target.h"
#include "Thunks.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

std::unique_ptr<Thunk> lld::elf::addThunkHaydn(Ctx &ctx,
                                               const InputSection &Isec,
                                               Relocation &Rel, Symbol &S) {
  switch (Rel.type) {
  case R_HAYDN_CallSImm20:
  case R_HAYDN_WIDE_CallSImm20:
  case R_HAYDN_WIDE_CallSImm20_E3E1:
  case R_HAYDN_BranchSImm16:
  case R_HAYDN_WIDE_BranchSImm12:
  case R_HAYDN_WIDE_BranchSImm12_E3E0:
  case R_HAYDN_WIDE_BranchSImm12_E3E1:
  case R_HAYDN_WIDE_BranchSImm12_E3E2:
  case R_HAYDN_WIDE_BranchSImm12_RI:
  case R_HAYDN_WIDE_BranchSImm12_RI_E3E0:
  case R_HAYDN_WIDE_BranchSImm12_RI_E3E1:
    // Exactly the kinds Haydn::needsThunk can arm, including the
    // D1.58 E3-qualified rows: every one fails closed with the same
    // named ABI gap. No R0-writing thunk bytes are emitted.
    Fatal(ctx) << Isec.getObjMsg(Rel.offset) << ": relocation " << Rel.type
               << " to '" << Rel.sym << "' needs a linker range-extension "
                  "veneer, but the Haydn veneer ABI is not approved "
                  "(D1.57 / ISA-70): the retired R0-borrowing template left "
                  "the JALR link in soft-zero R0. Refusing to emit R0-writing "
                  "thunk bytes; keep the branch/call within PC-relative range";
    llvm_unreachable("Fatal exits the link");
  default:
    Fatal(ctx) << "unrecognized relocation " << Rel.type << " to " << &S
               << " for Haydn target";
    llvm_unreachable("");
  }
}
