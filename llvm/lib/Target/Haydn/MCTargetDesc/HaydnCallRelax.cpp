//===-- HaydnCallRelax.cpp - Cycle-neutral call encoding relax ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnCallRelax.h"
#include "HaydnFormat.h"
#include "HaydnFormatERecords.h"
#include "HaydnMCTargetDesc.h"
#include "HaydnRelocLayout.h"

#include <cstring>

using namespace llvm;
using namespace llvm::haydn;
using namespace llvm::HaydnReloc;

namespace {

const format_e::FormatETypeLayoutRec *findLayout(uint8_t Mode, uint8_t EntryIdx,
                                                 const char *TypeName) {
  for (unsigned I = 0; I < format_e::FormatETypeLayoutCount; ++I) {
    const format_e::FormatETypeLayoutRec &L = format_e::FormatETypeLayouts[I];
    if (L.Mode == Mode && L.EntryIdx == EntryIdx && L.TypeName &&
        std::strcmp(L.TypeName, TypeName) == 0)
      return &L;
  }
  return nullptr;
}

const format_e::FormatETypeLayoutRec *firstLayout(uint8_t Mode,
                                                  uint8_t EntryIdx) {
  for (unsigned I = 0; I < format_e::FormatETypeLayoutCount; ++I) {
    const format_e::FormatETypeLayoutRec &L = format_e::FormatETypeLayouts[I];
    if (L.Mode == Mode && L.EntryIdx == EntryIdx)
      return &L;
  }
  return nullptr;
}

bool headerIsE2(const unsigned char *P, unsigned N) {
  if (N == 0)
    return false;
  // Indicator 111 at bits[2:0], entry_num 0 at bit 3.
  return (P[0] & 0x0fu) ==
         static_cast<unsigned char>(haydn::format::FormatEIndicatorBits);
}

bool e1PayloadEmpty(const unsigned char *P, unsigned N) {
  const format_e::FormatETypeLayoutRec *L = firstLayout(0, 1);
  if (!L || L->EntryHi < L->EntryLo)
    return false;
  const unsigned Width =
      static_cast<unsigned>(L->EntryHi) - static_cast<unsigned>(L->EntryLo) + 1u;
  return readField(P, N, Width, L->EntryLo) == 0;
}

const format_e::FormatEMemberRec *e0Inverse(const unsigned char *P,
                                            unsigned N) {
  if (!headerIsE2(P, N))
    return nullptr;
  for (unsigned I = 0; I < format_e::FormatETypeLayoutCount; ++I) {
    const format_e::FormatETypeLayoutRec &L = format_e::FormatETypeLayouts[I];
    if (L.Mode != 0 || L.EntryIdx != 0)
      continue;
    const unsigned TypeLo = static_cast<unsigned>(L.MapHi) + 1u;
    const unsigned TypeHi = TypeLo + L.TypeCodeWidth - 1u;
    if (TypeHi > L.EntryHi)
      continue;
    const uint64_t TypeCode = readField(P, N, L.TypeCodeWidth, TypeLo);
    if (TypeCode != L.TypeCode)
      continue;
    if (L.OpcodeHi < L.OpcodeLo)
      continue;
    const unsigned OpcodeBits =
        static_cast<unsigned>(L.OpcodeHi) - static_cast<unsigned>(L.OpcodeLo) +
        1u;
    const uint64_t Opcode = readField(P, N, OpcodeBits, L.OpcodeLo);
    const int Mid = format_e::findInverseMemberId(
        0, 0, L.Unit, static_cast<uint8_t>(TypeCode),
        static_cast<uint16_t>(Opcode));
    if (Mid < 0)
      continue;
    return &format_e::FormatEMembers[Mid];
  }
  return nullptr;
}

bool readE2E0GprEnc(const unsigned char *P, unsigned N, unsigned Lsb,
                    unsigned &Enc) {
  if (!P)
    return false;
  Enc = static_cast<unsigned>(
      readField(P, N, format_e::FormatEGPRFieldBits, Lsb));
  return true;
}

unsigned linkRegEnc() {
  return getHaydnSharedMCRegisterInfo().getEncodingValue(Haydn::R15);
}

} // namespace

namespace llvm {
namespace haydn {
namespace call_relax {

bool e2SingletonLogical(const unsigned char *P, unsigned N, const char *Name) {
  if (!P || !Name || N < format::canonicalFullSlotIdleParcel().size())
    return false;
  if (!e1PayloadEmpty(P, N))
    return false;
  const format_e::FormatEMemberRec *M = e0Inverse(P, N);
  return M && M->Logical && std::strcmp(M->Logical, Name) == 0;
}

bool readE2E0DestEnc(const unsigned char *P, unsigned N, unsigned &RtEnc) {
  return readE2E0GprEnc(P, N, format_e::FormatEE2E0DestLsb, RtEnc);
}

bool readE2E0RsEnc(const unsigned char *P, unsigned N, unsigned &RsEnc) {
  return readE2E0GprEnc(P, N, format_e::FormatEE2E0RsLsb, RsEnc);
}

bool isReturningCallRelaxTriple(const unsigned char *LuiP,
                                const unsigned char *AddiP,
                                const unsigned char *JalrP, unsigned N) {
  if (!e2SingletonLogical(LuiP, N, "LUI") ||
      !e2SingletonLogical(AddiP, N, "ADDI32") ||
      !e2SingletonLogical(JalrP, N, "JALR"))
    return false;
  unsigned RtEnc = 0, RsEnc = 0, LuiRd = 0, AddiRd = 0;
  if (!readE2E0DestEnc(JalrP, N, RtEnc) || RtEnc != linkRegEnc())
    return false;
  if (!readE2E0RsEnc(JalrP, N, RsEnc) || !readE2E0DestEnc(LuiP, N, LuiRd) ||
      !readE2E0DestEnc(AddiP, N, AddiRd))
    return false;
  return RsEnc == LuiRd && LuiRd == AddiRd;
}

bool writeE2JalSingleton(unsigned char *P, unsigned N, unsigned RtEnc) {
  ArrayRef<uint8_t> Idle = format::canonicalFullSlotIdleParcel();
  if (!P || N < Idle.size() ||
      RtEnc >= (1u << format_e::FormatEGPRFieldBits))
    return false;
  const format_e::FormatETypeLayoutRec *L = findLayout(0, 0, "I20");
  if (!L)
    return false;
  // JAL is I20 type-opcode 1 (NOP is I20 opcode 0). Member 215.
  const int Mid = format_e::findInverseMemberId(0, 0, L->Unit, L->TypeCode, 1);
  if (Mid < 0)
    return false;
  const format_e::FormatEMemberRec &JAL = format_e::FormatEMembers[Mid];
  if (!JAL.Logical || std::strcmp(JAL.Logical, "JAL") != 0)
    return false;

  std::memcpy(P, Idle.data(), Idle.size());
  if (N > Idle.size())
    std::memset(P + Idle.size(), 0, N - Idle.size());

  const unsigned TypeLo = static_cast<unsigned>(L->MapHi) + 1u;
  patchField(P, L->TypeCode, N, L->TypeCodeWidth, TypeLo);
  const unsigned OpcodeBits =
      static_cast<unsigned>(L->OpcodeHi) - static_cast<unsigned>(L->OpcodeLo) +
      1u;
  patchField(P, JAL.Opcode, N, OpcodeBits, L->OpcodeLo);
  patchField(P, RtEnc, N, format_e::FormatEGPRFieldBits,
             format_e::FormatEE2E0DestLsb);
  return true;
}

} // namespace call_relax
} // namespace haydn
} // namespace llvm
