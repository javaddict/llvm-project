//===- HaydnThunks.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn long-branch / long-call thunks (G-LLD-VENEER).
//
// Bundle128 parcels are 16 bytes. HI12/LO20 materialize the target into a
// borrowable soft-zero R0, then JALR jumps.
//
// Unified call + branch veneer — 3 parcels (48 B), no R12:
//   LUI    R0, hi12(target+addend)
//   ADDI32 R0, R0, lo20(...)
//   JALR   R0, R0, 0          // PC=target; R0=link (never falls through)
//
// Why R0, not R12:
//   * R0 is soft-zero and product law allows borrow (prologue re-zeros on
//     entry; mid-fn far branch leaves R0=link — documented residual).
//   * R12 is a normal caller-saved allocatable GPR — do not permanently
//     reserve it as linker AT (no free AT).
//   * Call sites already use JAL/JAL_W → LR (R15) holds the real return
//     address; veneer must not clobber LR. JALR rd=R0 preserves LR.
//   * Branch sites do not need a stack save of R12; R0 is free scratch.
//
// Relocation addend is honored. getThunkSectionSpacing() lives in Haydn.cpp.
//
//===----------------------------------------------------------------------===//

#include "HaydnThunks.h"

#include "InputSection.h"
#include "OutputSections.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "Thunks.h"
#include "lld/Common/CommonLinkerContext.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;
using namespace llvm::support;
using namespace lld;
using namespace lld::elf;

namespace {

/// Write a little-endian 128-bit word as 16 bytes.
static void writeBundle128LE(uint8_t *Buf, uint64_t Lo, uint64_t Hi) {
  endian::write64le(Buf, Lo);
  endian::write64le(Buf + 8, Hi);
}

static void writeLuiR0(uint8_t *Buf, uint32_t Imm12) {
  // lui r0, Imm12 — base Lo 0x000005c000000000; HI12 in bits[15:4]
  uint64_t Lo = 0x000005c000000000ull;
  Lo &= ~0xFFF0ull;
  Lo |= (static_cast<uint64_t>(Imm12 & 0xFFFu) << 4);
  writeBundle128LE(Buf, Lo, 0);
}

static void writeAddi32R0R0(uint8_t *Buf, uint32_t Imm20) {
  // addi32 r0, r0, Imm20 — base Lo 0x0000024000000000; LO20 in bits[37:18]
  uint64_t Lo = 0x0000024000000000ull;
  Lo &= ~(0xFFFFFull << 18);
  Lo |= (static_cast<uint64_t>(Imm20 & 0xFFFFFu) << 18);
  writeBundle128LE(Buf, Lo, 0);
}

static void writeJalrR0R0(uint8_t *Buf) {
  // jalr r0, r0, 0
  writeBundle128LE(Buf, 0x00000ec000000000ull, 0);
}

static void splitHiLo(uint64_t TargetVA, uint32_t &Hi12, uint32_t &Lo20) {
  uint64_t Hi = (TargetVA + 0x80000ull) >> 20;
  int64_t Lo =
      static_cast<int64_t>(TargetVA) - static_cast<int64_t>(Hi << 20);
  assert(Hi <= 0xFFFu && "HI12 out of range for LUI");
  assert(isInt<20>(Lo) && "LO20 out of range for ADDI32");
  Hi12 = static_cast<uint32_t>(Hi);
  Lo20 = static_cast<uint32_t>(Lo) & 0xFFFFFu;
}

/// Far call / far branch veneer. Borrows soft-zero R0; never touches R12/LR.
/// Size: 3 × 16 = 48 bytes.
class HaydnLongThunk : public Thunk {
public:
  HaydnLongThunk(Ctx &ctx, Relocation &rel, Symbol &dest)
      : Thunk(ctx, dest, rel.addend) {
    alignment = 16;
  }
  uint32_t size() override { return 48; }
  void writeTo(uint8_t *buf) override;
  void addSymbols(ThunkSection &isec) override;
};

void HaydnLongThunk::writeTo(uint8_t *Buf) {
  uint32_t Hi12, Lo20;
  splitHiLo(destination.getVA(ctx, addend), Hi12, Lo20);
  writeLuiR0(Buf + 0, Hi12);
  writeAddi32R0R0(Buf + 16, Lo20);
  writeJalrR0R0(Buf + 32);
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
