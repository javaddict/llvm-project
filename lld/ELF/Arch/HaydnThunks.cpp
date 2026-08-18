//===- HaydnThunks.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn long-branch / long-call linker veneers.
//
// Product text parcels use the production ObjectEncodingProfile EncodedBytes
// (Format E family; sole active row length today). HI12/LO20 materialize the
// absolute target into soft-zero R0, then JALR transfers control. One shape
// serves both far calls and far branches — three product parcels:
//
//   LUI    R0, hi12(target + addend)
//   ADDI32 R0, R0, lo20(...)
//   JALR   R0, R0, 0          // PC = target; R0 = link (never falls through)
//
// Parcel size and veneer length come only from the generated format registry
// (HaydnFormat EncodedBytes / maxEncodedBytesInProfile). Call sites must not
// spell parcel widths as bare literals.
//
// Why R0, not R12:
//   * R0 is reserved soft-zero; product law allows a temporary borrow.
//   * R12 is a normal caller-saved allocatable GPR — never a free linker AT.
//   * Call sites use JAL/JAL_W into LR (R15); veneer JALR rd=R0 preserves LR.
//   * Branch sites need no R12 stack save; R0 is the only scratch.
//
// Soft-zero residual (accepted ABI):
//   JALR writes PC_next into R0, so the landing site sees R0 == link, not 0.
//   * Far call into another function: callee prologue XOR32 r0,r0,r0 restores.
//   * Compiler JT / pure JALR rd=R0: ExpandPseudos re-zeros successors.
//   * Mid-function far branch through this veneer: R0 stays link until the
//     next known re-zero (epilogue before CSR restore, or an explicit site).
//     BranchRelaxation's insertIndirectBranch uses a scavenged scratch and
//     never discards the link into R0; this residual is linker-veneer only.
//
// Relocation addend is honored via Thunk(ctx, dest, rel.addend).
// Island spacing lives in Haydn::getThunkSectionSpacing() (Haydn.cpp).
//
// Wire bytes for the three singleton parcels are MC-verified Format E member
// encodings (llvm-mc -show-encoding of { lui/addi32/jalr r0,...; nop; nop }),
// not hand-transcribed. Geometry is sole product Format E (registry EncodedBytes
// = 12, 3-parcel veneer, Align-4). Non-12 parcel sizes fail closed.
//
//===----------------------------------------------------------------------===//

#include "HaydnThunks.h"

#include "InputSection.h"
#include "OutputSections.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "Thunks.h"
#include "HaydnFormat.h"
#include "lld/Common/CommonLinkerContext.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

#include <cassert>
#include <cstring>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;
using namespace llvm::support;
using namespace lld;
using namespace lld::elf;
using namespace llvm::haydn::format;

namespace {

/// Production parcel EncodedBytes from the object-encoding registry.
static EncodedBytes productParcelEncodedBytes() {
  return maxEncodedBytesInProfile(ObjectEncodingProfileID::E96);
}

static unsigned productParcelSize() {
  unsigned N = productParcelEncodedBytes().Value;
  // FE8: sole product is Format E 12-byte; reject residual 8/16 dual paths.
  // Never return 0: Thunk::alignment used to take this value and
  // ThunkSection::assignOffsets then aborted (alignToPowerOf2 Align==0).
  if (N != 12u)
    report_fatal_error("Haydn LLD: production EncodedBytes must be Format E 12",
                       /*GenCrashDiag=*/false);
  return N;
}

/// Three product parcels: LUI + ADDI32 + JALR (offsets 0 / N / 2N).
static unsigned longThunkBytes() { return 3u * productParcelSize(); }

/// Write one product parcel (EncodedBytes) little-endian from a 96-bit image
/// carried as low 64 + next 32. High bits beyond EncodedBits are ignored.
static void writeProductParcelLE(uint8_t *Buf, uint64_t Bits0_63,
                                 uint32_t Bits64_95) {
  const unsigned N = productParcelSize();
  uint8_t Tmp[16] = {};
  endian::write64le(Tmp, Bits0_63);
  endian::write32le(Tmp + 8, Bits64_95);
  assert(N <= sizeof(Tmp) && "product EncodedBytes exceeds write scratch");
  std::memcpy(Buf, Tmp, N);
}

static void writeLuiR0(uint8_t *Buf, uint32_t Imm12) {
  // lui r0, Imm12 — Format E singleton parcel (MC-verified, not the legacy
  // s0-window placeholder). HI12 field lives at parcel bits[43:32]:
  //   { lui r0, 0xfff; nop; nop } -> 07 0a 02 00 ff 0f 00 ..
  uint64_t Lo = 0x0000000000020a07ull;
  Lo &= ~(0xFFFull << 32);
  Lo |= (static_cast<uint64_t>(Imm12 & 0xFFFu) << 32);
  writeProductParcelLE(Buf, Lo, 0);
}

static void writeAddi32R0R0(uint8_t *Buf, uint32_t Imm20) {
  // addi32 r0, r0, Imm20 — Format E singleton parcel (MC-verified). LO20
  // field lives at parcel bits[50:31]:
  //   { addi32 r0,r0,-1; nop; nop } -> 07 0f 02 80 ff ff 07 00 ..
  uint64_t Lo = 0x0000000000020f07ull;
  Lo &= ~(0xFFFFFull << 31);
  Lo |= (static_cast<uint64_t>(Imm20 & 0xFFFFFu) << 31);
  writeProductParcelLE(Buf, Lo, 0);
}

static void writeJalrR0R0(uint8_t *Buf) {
  // jalr r0, r0, 0 — Format E singleton parcel (MC-verified); link lands in
  // R0 (soft-zero residual). { jalr r0,r0,0; nop; nop } -> 07 0d 02 00 ..
  writeProductParcelLE(Buf, 0x0000000000020d07ull, 0);
}

static void splitHiLo(uint64_t TargetVA, uint32_t &Hi12, uint32_t &Lo20) {
  // HI12/LO20 split matches MC HI12/LO20 (VA + 0x80000) >> 20.
  uint64_t Hi = (TargetVA + 0x80000ull) >> 20;
  int64_t Lo =
      static_cast<int64_t>(TargetVA) - static_cast<int64_t>(Hi << 20);
  assert(Hi <= 0xFFFu && "HI12 out of range for LUI");
  assert(isInt<20>(Lo) && "LO20 out of range for ADDI32");
  Hi12 = static_cast<uint32_t>(Hi);
  Lo20 = static_cast<uint32_t>(Lo) & 0xFFFFFu;
}

/// Far call / far branch veneer. Borrows soft-zero R0; never touches R12/LR.
/// Size: 3 × product EncodedBytes (parcel-grid veneer). Alignment is 4, the
/// Hexagon packet/function lattice (HexagonThunk, Thunks.cpp:427). AIE can
/// use Align(16) because that bundle width is 2^n; EncodedBytes=12 is not,
/// so it is a size/phase modulus only — feeding it to Thunk::alignment
/// aborts assignOffsets (alignToPowerOf2). Island spacing is 0 mod
/// EncodedBytes and compiler text is whole parcels, so Align-4 entries
/// stay on the section's parcel phase.
class HaydnLongThunk : public Thunk {
public:
  HaydnLongThunk(Ctx &ctx, Relocation &rel, Symbol &dest)
      : Thunk(ctx, dest, rel.addend) {
    alignment = 4;
  }
  uint32_t size() override { return longThunkBytes(); }
  void writeTo(uint8_t *buf) override;
  void addSymbols(ThunkSection &isec) override;
};

void HaydnLongThunk::writeTo(uint8_t *Buf) {
  const unsigned Parcel = productParcelSize();
  uint32_t Hi12, Lo20;
  splitHiLo(destination.getVA(ctx, addend), Hi12, Lo20);
  writeLuiR0(Buf + 0, Hi12);
  writeAddi32R0R0(Buf + Parcel, Lo20);
  writeJalrR0R0(Buf + 2u * Parcel);
}

void HaydnLongThunk::addSymbols(ThunkSection &Isec) {
  addSymbol(ctx.saver.save("__haydn_thunk_" + destination.getName()), STT_FUNC,
            0, Isec);
}

} // namespace

std::unique_ptr<Thunk> lld::elf::addThunkHaydn(Ctx &ctx,
                                               const InputSection &Isec,
                                               Relocation &Rel, Symbol &S) {
  (void)Isec;
  switch (Rel.type) {
  case R_HAYDN_CallSImm20:
  case R_HAYDN_WIDE_CallSImm20:
  case R_HAYDN_BranchSImm16:
  case R_HAYDN_WIDE_BranchSImm12:
  case R_HAYDN_WIDE_BranchSImm12_RI:
    return std::make_unique<HaydnLongThunk>(ctx, Rel, S);
  default:
    Fatal(ctx) << "unrecognized relocation " << Rel.type << " to " << &S
               << " for Haydn target";
    llvm_unreachable("");
  }
}
