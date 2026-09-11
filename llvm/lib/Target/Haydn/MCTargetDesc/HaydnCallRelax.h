//===-- HaydnCallRelax.h - Cycle-neutral call encoding relax ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RISC-V peer: LLD relaxCall may delete AUIPC and shrink a call to jal
// (bytes and fetch cycles both drop). Haydn overlay: ISel emits the general
// call (LUI HI12 + ADDI32 LO20 + JALR). When |disp| fits CallSImm20, LLD
// may rewrite the JALR parcel in place to generated E2 JAL (same link
// dest). LUI+ADDI stay — they write the address temp; later jalr lr, temp
// still reads it (CoreMark iterate crc). Idling those parcels is the
// AUIPC-delete RISC-V is allowed and Haydn is not.
//
// Packet count and cycle count stay identical. On E96 every product
// packet is 12 bytes, so the rewrite is also byte-neutral until an
// unequal-width family is admitted.
//
// This is encoding relaxation, not an LLD veneer (D1.57 / ISA-70). Mixed
// packets (a sibling packed with LUI/ADDI/JALR) and E3 rows are left on
// the general form — always reachable, never a cycle collapse.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNCALLRELAX_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNCALLRELAX_H

namespace llvm {
namespace haydn {
namespace call_relax {

/// True iff \p P[0, N) is an E2 parcel whose e0 inverse logical is \p Name
/// and whose e1 payload is empty (generated NOP / underfill).
bool e2SingletonLogical(const unsigned char *P, unsigned N, const char *Name);

/// Read the generated E2 e0 dest encoding (JAL / JALR link register).
bool readE2E0DestEnc(const unsigned char *P, unsigned N, unsigned &RtEnc);

/// Read the generated E2 e0 JALR/ADDI rs encoding (FormatEE2E0RsLsb).
bool readE2E0RsEnc(const unsigned char *P, unsigned N, unsigned &RsEnc);

/// True iff the three E2 parcels are a returning-call relax candidate:
/// LUI+ADDI32+JALR e0 singletons, JALR.rd is LR (MRI encoding of R15), and
/// JALR.rs == LUI.rd == ADDI.rd. Long jumps (rd != LR) and coincidental
/// JALR after a different LUI+ADDI stay on the general form.
bool isReturningCallRelaxTriple(const unsigned char *LuiP,
                                const unsigned char *AddiP,
                                const unsigned char *JalrP, unsigned N);

/// Write a complete E2 JAL singleton (header + I20 JAL e0 + empty e1).
/// Immediate is 0; the caller patches WIDE_CallSImm20 with RelocLayout.
bool writeE2JalSingleton(unsigned char *P, unsigned N, unsigned RtEnc);

} // namespace call_relax
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNCALLRELAX_H
