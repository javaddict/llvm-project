//===- Haydn.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn is a 3-issue VLIW DSP architecture. This file handles ELF linking
// for Haydn targets (baremetal, 32-bit little-endian).
//
// Relocation overview:
//   R_HAYDN_BranchSImm16 — 16-bit signed PC-relative branch (word-aligned).
//     Range: [-128KB, +128KB). If out of range, a thunk (long branch sequence)
//     is inserted.
//   R_HAYDN_CallSImm20 — 20-bit signed PC-relative call (halfword-aligned).
//     Range: [-1MB, +1MB). If out of range, a thunk is inserted.
//   R_HAYDN_HI20/LO16 — LUI+ADDI32 pair for 32-bit absolute addressing.
//   R_HAYDN_GOT_HI20 — GOT entry address high part.
//   R_HAYDN_TPREL_HI20/LO16 — TLS TP-relative offsets.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Symbols.h"
#include "Target.h"
#include "HaydnRelocLayout.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {

class Haydn final : public TargetInfo {
public:
  Haydn(Ctx &);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  RelType getDynRel(RelType type) const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
  bool needsThunk(RelExpr expr, RelType type, const InputFile *file,
                  uint64_t branchAddr, const Symbol &s,
                  int64_t a) const override;
  bool inBranchRange(RelType type, uint64_t src, uint64_t dst) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
  uint32_t calcEFlags() const override;
  // Pre-create ThunkSections so far sites inside a large .text can still
  // reach an island (G-LLD-VENEER V1). Sized from the narrowest thunked
  // reloc: WIDE_BranchSImm12 / _RI → isInt<13> ≈ ±4 KiB.
  uint32_t getThunkSectionSpacing() const override { return 0x1000; }
};

} // namespace

Haydn::Haydn(Ctx &ctx) : TargetInfo(ctx) {
  // 32-bit, little-endian, baremetal
  pltRel = R_HAYDN_NONE;
  relativeRel = R_HAYDN_32;
  gotRel = R_HAYDN_32;
  symbolicRel = R_HAYDN_32;
  pltHeaderSize = 0;
  pltEntrySize = 0;
  defaultMaxPageSize = 4096;

  // Enable thunk generation for long branches/calls that exceed the
  // signed 16-bit (branch) or signed 20-bit (call) PC-relative range.
  needsThunks = true;

  // Trap instruction (4 bytes of NOP)
  trapInstr = {0x00, 0x00, 0x00, 0x00};

  // NOP instructions for alignment padding (16-bit and 32-bit variants).
  // F38: verified against encoding_manual.md section 10 "NOP Encodings":
  //   - 16-bit canonical Bundle NOP is `0x0000` (section 10 "Canonical NOP
  //     Bundles": "Bundle NOP | 16-bit 0x0000 | 2B | Default"). The all-zero
  //     slot aliases to NOP because FU-select opcode 0 is reserved for NOP
  //     (section 10 "Opcode 0 -- Reserved for NOP"), so any all-zero slot
  //     window decodes as a per-slot NOP regardless of format.
  //   - 32-bit NOP is the same all-zero pattern (section 10 "Per-Slot NOP":
  //     s0/s1 14-bit windows = 14'b0 inside a 32-bit bundle; an all-zero
  //     32-bit word has the 16-bit NOP as its low half and a NOP-equivalent
  //     slot as its high half, so 0x0000'0000 is a safe 32-bit NOP for
  //     alignment padding).
  nopInstrs = {{0x00, 0x00}, {0x00, 0x00, 0x00, 0x00}};
}

uint32_t Haydn::calcEFlags() const {
  // Baremetal Haydn: no special ELF flags to merge.
  // If any input has Haydn-specific flags (e.g. ISA version), take the max.
  uint32_t flags = 0;
  for (InputFile *f : ctx.objectFiles) {
    uint32_t eflags =
        cast<ObjFile<ELF32LE>>(f)->getObj().getHeader().e_flags;
    if (eflags > flags)
      flags = eflags;
  }
  return flags;
}

RelExpr Haydn::getRelExpr(RelType type, const Symbol &s,
                          const uint8_t *loc) const {
  switch (type) {
  case R_HAYDN_32_PCREL:
  case R_HAYDN_PC_LO20:
  case R_HAYDN_BranchSImm16:
  case R_HAYDN_CallSImm20:
  case R_HAYDN_WIDE_BranchSImm12:
  case R_HAYDN_WIDE_BranchSImm12_RI: // CB-94: RI12 BNE_W/BEQ_W/…
  case R_HAYDN_WIDE_CallSImm20:
  case R_HAYDN_HWLoopOff1:
  case R_HAYDN_HWLoopOff2:
    // WIDE SET_HWLOOP_W offset fields are PC-relative (loop-body start/end
    // minus the SET_HWLOOP pc), same expr class as branches. L228-class gap:
    // these relocs are emitted as external ELF relocs by HaydnELFObjectWriter
    // but were missing here, so any cross-section hwloop target hit getRelExpr's
    // default -> "unknown Haydn relocation type".
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
    // F07/codex-3: unknown relocations must not be silently classified as
    // R_ABS — that would link malformed objects with incorrect semantics.
    // Diagnose and treat as no-op instead of guessing.
    Err(ctx) << "unknown Haydn relocation type: " << type;
    return R_NONE;
  }
}

RelType Haydn::getDynRel(RelType type) const {
  // Only R_HAYDN_32 is a valid dynamic relocation for baremetal.
  if (type == R_HAYDN_32)
    return type;
  return R_HAYDN_NONE;
}

int64_t Haydn::getImplicitAddend(const uint8_t *buf, RelType type) const {
  // Single-source reader: delegate to HaydnRelocLayout — the SAME table the MC
  // backend's applyFixup and this file's relocate() consult. Reader and writer
  // share rows, so they cannot diverge (CB-76 reader/writer ÷2/÷4 split killed).
  // R_HAYDN_* values 0..20 map 1:1 to HaydnReloc's shared members.
  if (type > R_HAYDN_WIDE_BranchSImm12_RI) {
    InternalErr(ctx, buf) << "cannot read addend for relocation " << type;
    return 0;
  }
  return HaydnReloc::readRelocAddend(static_cast<HaydnReloc::RelocKind>(static_cast<unsigned>(type)), buf);
}

bool Haydn::inBranchRange(RelType type, uint64_t src, uint64_t dst) const {
  int64_t offset = dst - src;
  switch (type) {
  case R_HAYDN_BranchSImm16:
    // 16-bit signed offset, shifted left 1 for 2-byte word alignment (Path B
    // / D1, encoding_manual.md §5.14). Effective range: [-64KB, +64KB) from
    // source. The previous ÷4 granularity (18-bit, 4-byte aligned, ±128KB)
    // was STRICTER than the spec and broke 2-byte-aligned targets after a
    // true 6-byte 48-bit parcel.
    return isInt<17>(offset);
  case R_HAYDN_CallSImm20:
    // Bundle128 JAL_S0 (CallSImm20): signed 20-bit PC-relative BYTE offset
    // (ValueShift=0 / calltarget_s0). Range: [-512KiB, +512KiB).
    return isInt<20>(offset);
  case R_HAYDN_WIDE_CallSImm20:
    // WIDE JAL_W: 20-bit field × halfword (÷2) → effective 21-bit byte range.
    return isInt<21>(offset);
  case R_HAYDN_WIDE_BranchSImm12:
  case R_HAYDN_WIDE_BranchSImm12_RI:
    // Bundle128 I12/RI12 branch: 12-bit field x 2-byte alignment (D1) = 13-bit
    // effective range. Narrower than the 16-bit narrow branch.
    return isInt<13>(offset);
  case R_HAYDN_HWLoopOff1:
    // WIDE SET_HWLOOP_W Off1: 6-bit field x 2-byte alignment (D1) = 7-bit
    // effective range. Loop-body start is always within a few bytes -> in range.
    return isInt<7>(offset);
  case R_HAYDN_HWLoopOff2:
    // WIDE SET_HWLOOP_W Off2: 12-bit field x 2-byte alignment = 13-bit range.
    return isInt<13>(offset);
  default:
    return true;
  }
}

bool Haydn::needsThunk(RelExpr expr, RelType type, const InputFile *file,
                       uint64_t branchAddr, const Symbol &s,
                       int64_t a) const {
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

void Haydn::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  // Single-source writer: delegate to HaydnRelocLayout — the SAME table the MC
  // backend's applyFixup consults. Branch=÷2, hwloop=÷4, HI/LO MIPS-split all
  // live in one place; the field is patched geometrically so ONLY the imm bits
  // are touched (CB-76: no more flat 0xFFFF mask clobbering rd/opcode, and no
  // stale ÷4 on the branch writer).
  RelType type = rel.type;
  if (type > R_HAYDN_WIDE_BranchSImm12_RI) {
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << type;
    return;
  }
  HaydnReloc::RelocKind R = static_cast<HaydnReloc::RelocKind>(static_cast<unsigned>(type));
  HaydnReloc::RelocCompute Comp = HaydnReloc::computeRelocValue(R, val);
  if (!Comp.OK) {
    Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << type << ": "
             << Comp.Err;
    return;
  }
  const HaydnReloc::RelocFieldInfo &FI = HaydnReloc::getRelocFieldInfo(R);
  HaydnReloc::patchField(loc, Comp.FieldVal, FI.NBytes, FI.FieldSize, FI.FieldLsb);
}

void elf::setHaydnTargetInfo(Ctx &ctx) {
  ctx.target.reset(new Haydn(ctx));
}
