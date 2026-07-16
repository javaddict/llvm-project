//===- HaydnThunks.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn long-branch thunk. Extracted from the shared upstream Thunks.cpp so
// that shared file only retains a minimal `case EM_HAYDN:` dispatch case that
// forwards to `addThunkHaydn()` (declared in HaydnThunks.h). This satisfies
// HC#0 ("Never modify upstream LLVM files except via Haydn-specific
// subdirectories").
//
// When a branch (R_HAYDN_BranchSImm16 / R_HAYDN_WIDE_BranchSImm12[_RI]) or
// call (R_HAYDN_CallSImm20 / R_HAYDN_WIDE_CallSImm20) target is out of
// range, this thunk is inserted.
//
// Bundle128-only emission (D456 / D489, post multi-width retire):
// every encoded parcel is 16 bytes `{s2[39:0], s1[39:0], s0[47:0]}` with idle
// slots zero. The long-call sequence is three Bundle128 parcels (48 bytes):
//
//   LUI    R12, hi12(target)       ; bits[31:20] = imm12  (HI12 @ s0 bits[15:4])
//   ADDI32 R12, R12, lo20(target)  ; bits[19:0]  += sext(imm20)
//   JALR   R0,  R12, 0             ; PC = R12  (link discarded)
//
// HI/LO split is the ISA-43 / CB-117 convention (pairs with MC HI12/LO20):
//   HI12 = (VA + 0x80000) >> 20
//   LO20 = VA - (HI12 << 20)          // signed 20-bit; ADDI32 sign-extends
// Reconstruction: (HI12 << 20) + sext(LO20) = VA.
//
// R12 is the reserved linker/assembler scratch ("AT", D177/L145): never an
// argument or return register. Mirrors ARM `ip` / MIPS `at`.
//
// The previous 12-byte multi-width LUI_W+JALR_W path (6+6 WIDE parcels) is
// retired: the Bundle128 decoder treats those bytes as a single 16-byte
// window and prints `<unknown>`, and CodeGen never emits that layout.
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

/// Bundle128 long-call thunk: LUI + ADDI32 + JALR, three 16-byte parcels.
class HaydnThunk : public Thunk {
public:
  HaydnThunk(Ctx &ctx, const InputSection &isec, Relocation &rel, Symbol &dest)
      : Thunk(ctx, dest, 0), relOffset(rel.offset) {
    // Bundle128 parcels are 16-byte aligned (D487 size model).
    alignment = 16;
  }
  uint32_t relOffset;
  uint32_t size() override { return 48; }
  void writeTo(uint8_t *buf) override;
  void addSymbols(ThunkSection &isec) override;
};

/// Write a little-endian 128-bit word as 16 bytes.
static void writeBundle128LE(uint8_t *Buf, uint64_t Lo, uint64_t Hi) {
  endian::write64le(Buf, Lo);
  endian::write64le(Buf + 8, Hi);
}

/// Bundle128 LUI R12, Imm12. Template from llvm-mc for `lui r12, 0`; HI12
/// field is LoWord bits[15:4] (HaydnRelocLayout HI12 / D489).
static void writeLuiR12(uint8_t *Buf, uint32_t Imm12) {
  // lui r12, 0 → 0c 00 00 00 c0 05 00 00 00 00 00 00 00 00 00 00
  // LE u64 of first 8 bytes: 0x000005c00000000c
  uint64_t Lo = 0x000005c00000000cull;
  Lo &= ~0xFFF0ull;
  Lo |= (static_cast<uint64_t>(Imm12 & 0xFFFu) << 4);
  writeBundle128LE(Buf, Lo, 0);
}

/// Bundle128 ADDI32 R12, R12, Imm20. Template from llvm-mc for
/// `addi32 r12, r12, 0`; LO20 field is LoWord bits[37:18].
static void writeAddi32R12(uint8_t *Buf, uint32_t Imm20) {
  // addi32 r12, r12, 0 → cc 00 00 00 40 02 00 00 00 00 00 00 00 00 00 00
  // LE u64 of first 8 bytes: 0x00000240000000cc
  uint64_t Lo = 0x00000240000000ccull;
  Lo &= ~(0xFFFFFull << 18);
  Lo |= (static_cast<uint64_t>(Imm20 & 0xFFFFFu) << 18);
  writeBundle128LE(Buf, Lo, 0);
}

/// Bundle128 JALR R0, R12, 0 (discard link, jump through AT).
static void writeJalrR0R12(uint8_t *Buf) {
  // jalr r0, r12, 0 → 0c 00 00 00 c0 0e 00 00 00 00 00 00 00 00 00 00
  // LE u64 of first 8 bytes: 0x00000ec00000000c
  writeBundle128LE(Buf, 0x00000ec00000000cull, 0);
}

} // namespace

void HaydnThunk::writeTo(uint8_t *Buf) {
  uint64_t TargetVA = destination.getVA(ctx, addend);

  // HI12/LO20 MIPS-style split (matches HaydnRelocLayout Hi12/Lo20 + CB-117).
  uint64_t Hi = (TargetVA + 0x80000ull) >> 20;
  int64_t Lo = static_cast<int64_t>(TargetVA) -
               static_cast<int64_t>(Hi << 20);
  assert(Hi <= 0xFFFu && "HI12 out of range for LUI");
  assert(isInt<20>(Lo) && "LO20 out of range for ADDI32");

  writeLuiR12(Buf + 0, static_cast<uint32_t>(Hi));
  writeAddi32R12(Buf + 16, static_cast<uint32_t>(Lo) & 0xFFFFFu);
  writeJalrR0R12(Buf + 32);
}

void HaydnThunk::addSymbols(ThunkSection &Isec) {
  addSymbol(ctx.saver.save("__haydn_thunk_" + destination.getName()), STT_FUNC,
            0, Isec);
}

// Factory invoked from the shared Thunks.cpp dispatch case for EM_HAYDN.
// Keep this switch in sync with Haydn::needsThunk — every RelType that can
// request a thunk must be constructible here.
std::unique_ptr<Thunk> lld::elf::addThunkHaydn(Ctx &ctx,
                                               const InputSection &Isec,
                                               Relocation &Rel, Symbol &S) {
  switch (Rel.type) {
  case R_HAYDN_BranchSImm16:
  case R_HAYDN_CallSImm20:
  case R_HAYDN_WIDE_BranchSImm12:
  case R_HAYDN_WIDE_BranchSImm12_RI:
  case R_HAYDN_WIDE_CallSImm20: // CB-112: JAL_W soft-div/libcall far reach
    return std::make_unique<HaydnThunk>(ctx, Isec, Rel, S);
  default:
    Fatal(ctx) << "unrecognized relocation " << Rel.type << " to " << &S
               << " for Haydn target";
    llvm_unreachable("");
  }
}
