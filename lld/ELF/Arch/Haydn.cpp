//===- Haydn.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn is a VLIW DSP architecture. This file handles ELF linking for Haydn
// targets (baremetal, 32-bit little-endian).
//
// Product encoding profile is Format E (sole active family): parcel size is
// the generated EncodedBytes from the object-encoding registry (HaydnFormat).
// Layout/veneers/padding geometry must not scatter bare parcel-width literals.
//
// Relocation overview:
//   Alignment, scale, and field range live only in HaydnRelocLayout
//   (computeRelocValue). Branch scale remains fail-closed / table-driven until
//   golden closes the wire unit; consumers must not invent a scale. Summary of
//   common PC-rel kinds:
//   R_HAYDN_BranchSImm16 — signed branch field (range via HaydnRelocLayout).
//   R_HAYDN_CallSImm20 — signed call field (range via HaydnRelocLayout).
//   R_HAYDN_WIDE_BranchSImm12/_RI — narrow signed branch field.
//   R_HAYDN_WIDE_CallSImm20 — wide signed call field.
//   R_HAYDN_HWLoopOff1/Off2 — unsigned Format E SET_HWLOOP displacement
//     fields (<<2 law; FieldLsb via HaydnRelocLayout / resolveFieldLsb).
//   Out-of-range branch/call sites get long-branch thunks (needsThunk).
//   R_HAYDN_HI20/LO16 — LUI+ADDI32 pair for 32-bit absolute addressing.
//   R_HAYDN_GOT_HI20 — GOT entry address high part.
//   R_HAYDN_TPREL_HI20/LO16 — TLS TP-relative offsets.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "InputSection.h"
#include "Symbols.h"
#include "Target.h"
#include "HaydnFormat.h"
#include "HaydnRelocLayout.h"
#include "llvm/ADT/bit.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

#include <vector>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {

/// Production parcel EncodedBytes (sole active profile: Format E / 12).
static llvm::haydn::format::EncodedBytes productParcelEncodedBytes() {
  auto B = llvm::haydn::format::maxEncodedBytesInProfile(
      llvm::haydn::format::ObjectEncodingProfileID::E96);
  // FE8: reject residual 8/16 dual product sizes at the LLD boundary.
  assert(B.Value == 12u && "LLD production EncodedBytes must be Format E 12");
  return B;
}

// Named HaydnELF (not Haydn) so out-of-line member defs do not collide with
// namespace llvm::Haydn pulled in via MCTargetDesc headers.
class HaydnELF final : public TargetInfo {
public:
  HaydnELF(Ctx &CtxRef) : TargetInfo(CtxRef) {
    // 32-bit, little-endian, baremetal
    pltRel = R_HAYDN_NONE;
    relativeRel = R_HAYDN_32;
    gotRel = R_HAYDN_32;
    symbolicRel = R_HAYDN_32;
    pltHeaderSize = 0;
    pltEntrySize = 0;
    defaultMaxPageSize = 4096;

    // Enable thunk generation for long branches/calls whose PC-relative
    // offsets fail HaydnRelocLayout::computeRelocValue (inBranchRange).
    needsThunks = true;

    // Trap filler is a fixed 4-byte TargetInfo field. Prefer whole-parcel
    // zeros for executable padding via nopInstrs (below).
    trapInstr = {0x00, 0x00, 0x00, 0x00};

    // Executable padding: whole product parcels only.
    //
    // Canonical idle wire bytes are blocked until golden closes completion /
    // top-pad policy; the vectors ledger forbids driving LLD idle pad from
    // stubs. Until then, LLD only accepts fill sizes that are multiples of
    // the production EncodedBytes and writes zero bytes as a fail-closed
    // geometry placeholder — not a product-legal Format E idle claim
    // (indicator 000 is not Format E). Partial 2/4-byte linker NOPs are
    // retired so text packing stays congruent with BundleSim ASSERT %
    // product-record-size.
    const unsigned Parcel = productParcelEncodedBytes().Value;
    std::vector<uint8_t> WholeParcel(Parcel, 0);
    nopInstrs = std::vector<std::vector<uint8_t>>{std::move(WholeParcel)};
  }

  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override {
    switch (type) {
    case R_HAYDN_32_PCREL:
    case R_HAYDN_PC_LO20:
    case R_HAYDN_BranchSImm16:
    case R_HAYDN_CallSImm20:
    case R_HAYDN_WIDE_BranchSImm12:
    case R_HAYDN_WIDE_BranchSImm12_RI:
    case R_HAYDN_WIDE_CallSImm20:
    case R_HAYDN_HWLoopOff1:
    case R_HAYDN_HWLoopOff2:
      return R_PC;
    case R_HAYDN_NONE:
      return R_NONE;
    case R_HAYDN_32:
    case R_HAYDN_8:
    case R_HAYDN_16:
    case R_HAYDN_SImm16:
    case R_HAYDN_HI20:
    case R_HAYDN_LO16:
    case R_HAYDN_HI12:
    case R_HAYDN_LO20:
    case R_HAYDN_TPREL_HI20:
    case R_HAYDN_TPREL_LO16:
      return R_ABS;
    case R_HAYDN_GOT_HI20:
      return R_GOT;
    default:
      Err(ctx) << "unknown Haydn relocation type: " << type;
      return R_NONE;
    }
  }

  RelType getDynRel(RelType type) const override {
    if (type == R_HAYDN_32)
      return type;
    return R_HAYDN_NONE;
  }

  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override {
    if (type > R_HAYDN_WIDE_BranchSImm12_RI) {
      InternalErr(ctx, buf) << "cannot read addend for relocation " << type;
      return 0;
    }
    return HaydnReloc::readRelocAddend(
        static_cast<HaydnReloc::RelocKind>(static_cast<unsigned>(type)), buf);
  }

  bool inBranchRange(RelType type, uint64_t src, uint64_t dst) const override {
    switch (type) {
    case R_HAYDN_BranchSImm16:
    case R_HAYDN_CallSImm20:
    case R_HAYDN_WIDE_CallSImm20:
    case R_HAYDN_WIDE_BranchSImm12:
    case R_HAYDN_WIDE_BranchSImm12_RI:
    case R_HAYDN_HWLoopOff1:
    case R_HAYDN_HWLoopOff2: {
      HaydnReloc::RelocKind R =
          static_cast<HaydnReloc::RelocKind>(static_cast<unsigned>(type));
      int64_t Offset = static_cast<int64_t>(dst - src);
      return HaydnReloc::computeRelocValue(R, static_cast<uint64_t>(Offset)).OK;
    }
    default:
      return true;
    }
  }

  bool needsThunk(RelExpr expr, RelType type, const InputFile *file,
                  uint64_t branchAddr, const Symbol &s,
                  int64_t a) const override {
    switch (type) {
    case R_HAYDN_BranchSImm16:
    case R_HAYDN_CallSImm20:
    case R_HAYDN_WIDE_BranchSImm12:
    case R_HAYDN_WIDE_BranchSImm12_RI:
    case R_HAYDN_WIDE_CallSImm20:
      return !inBranchRange(type, branchAddr, s.getVA(ctx, a));
    default:
      return false;
    }
  }

  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override {
    RelType type = rel.type;
    if (type > R_HAYDN_WIDE_BranchSImm12_RI) {
      Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << type;
      return;
    }
    HaydnReloc::RelocKind R =
        static_cast<HaydnReloc::RelocKind>(static_cast<unsigned>(type));
    HaydnReloc::RelocCompute Comp = HaydnReloc::computeRelocValue(R, val);
    if (!Comp.OK) {
      Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << type << ": "
               << Comp.Err;
      return;
    }
    const HaydnReloc::RelocFieldInfo &FI = HaydnReloc::getRelocFieldInfo(R);
    // WIDE_CallSImm20: E2 e0 imm @ [31:50]; E3 e0/e1 via resolveFieldLsb.
    const unsigned FieldLsb = HaydnReloc::resolveFieldLsb(R, loc);
    HaydnReloc::patchField(loc, Comp.FieldVal, FI.NBytes, FI.FieldSize,
                           FieldLsb);
  }

  uint32_t calcEFlags() const override {
    // Product output always carries production ELFFlagsValue (nonzero).
    // Inputs with e_flags==0 are transitional yaml/pre-flag objects and are
    // upgraded only when no conflicting nonzero flag is present.
    const uint32_t Expected =
        llvm::haydn::format::getProductionObjectEncodingProfile().ELFFlagsValue;
    assert(Expected != 0 && "E96 product profile must allocate nonzero e_flags");
    bool SeenZero = false;
    bool SeenExpected = false;
    for (InputFile *f : ctx.objectFiles) {
      uint32_t eflags =
          cast<ObjFile<ELF32LE>>(f)->getObj().getHeader().e_flags;
      if (eflags == 0) {
        SeenZero = true;
        continue;
      }
      if (eflags != Expected) {
        ErrAlways(ctx) << f << ": incompatible e_flags 0x"
                       << Twine::utohexstr(eflags)
                       << "; expected Format E ABI flag 0x"
                       << Twine::utohexstr(Expected);
        continue;
      }
      SeenExpected = true;
    }
    // Zero e_flags inputs are transitional (pre-flag crt/sysroot/yaml).
    (void)SeenZero;
    (void)SeenExpected;
    return Expected;
  }

  // Pre-create ThunkSections so far sites inside a large .text can still
  // reach an island. Narrowest thunked reloc is WIDE_BranchSImm12 / _RI.
  // Three-parcel veneers (3 × EncodedBytes) still fit in the margin.
  uint32_t getThunkSectionSpacing() const override {
    return 0x1000 - 0x400; // 3 KiB islands; ~1 KiB headroom for veneers
  }
};

} // namespace

void elf::setHaydnTargetInfo(Ctx &ctx) {
  ctx.target.reset(new HaydnELF(ctx));
}
