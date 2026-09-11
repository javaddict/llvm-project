//===- HaydnLayoutSite.h - closure-local control site table -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Transient typed site record for post-commit exact-layout closure.
// ARM ImmBranch (ARMConstantIslandPass.cpp:188-197) is MI+MaxDisp only.
// AIE has no BR and no site table (AIE2TargetMachine.cpp:92 empty PreEmit;
// AIEBaseInstrInfo.cpp:186-188 indirect unanalyzable). Hexagon packetizes
// last (HexagonTargetMachine.cpp:482-493) so HexagonBranchRelaxation.cpp:
// 96-129/164-180 and HexagonFixupHwLoops.cpp:98-157 are separate opcode
// walks with no packet identity. RISC-V insertIndirectBranch RestoreBB
// (RISCVInstrInfo.cpp:1433-1471) is the CFG form Haydn must not grow
// post-commit.
//
// Haydn overlay: committed Root/Member (bare Root==Member pre-stamp) +
// Dest/Dest2 + published HaydnReloc::RelocFieldInfo row + monotone Rank +
// already-admitted template id. Not an MFI field, not a pass, not persisted.
// Rebuild while recomputing layout. raiseRank is fatal on decrease.
// JALRSImm12 is schema identity only (ISA-BRANCH fail-closed).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNLAYOUTSITE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNLAYOUTSITE_H

#include "MCTargetDesc/HaydnRelocLayout.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <string>

namespace llvm {

class HaydnInstrInfo;
class MachineBasicBlock;
class MachineFunction;
class MachineInstr;

namespace haydn {

enum class LayoutSiteKind : uint8_t { Branch, Call, HWLoop };

enum class LayoutSiteRank : uint8_t {
  FitPatch = 0,
  NeutralizedNop = 1,
  LongTemplate = 2,
  SafeMaximal = 3,
};

enum class LayoutTemplateId : uint8_t {
  None = 0,
  InBlockJalr,
  InvertNearCondJalr,
  HwLoopSoftLatch,
  HwLoopSoftLongLatch,
};

struct LayoutSite {
  LayoutSiteKind Kind = LayoutSiteKind::Branch;
  MachineInstr *Root = nullptr;
  MachineInstr *Member = nullptr;
  MachineBasicBlock *Dest = nullptr;
  MachineBasicBlock *Dest2 = nullptr;
  HaydnReloc::RelocKind FieldKind = HaydnReloc::RelocKind::Invalid;
  HaydnReloc::RelocKind FieldKind2 = HaydnReloc::RelocKind::None;
  LayoutSiteRank Rank = LayoutSiteRank::FitPatch;
  LayoutTemplateId Template = LayoutTemplateId::None;

  const HaydnReloc::RelocFieldInfo &fieldInfo() const;
  void raiseRank(LayoutSiteRank NewRank);
  bool canFitPatch() const;
};

class LayoutSiteTable {
  SmallVector<LayoutSite, 8> Sites;
  DenseMap<const MachineInstr *, unsigned> ByMember;

public:
  void clear();

  /// Bind every branch/call/HWLoop site to a published RelocFieldInfo row.
  /// Unknown control and symbolic JALR fail closed (false, \p Err set).
  bool collect(MachineFunction &MF, const HaydnInstrInfo &TII, std::string &Err);

  LayoutSite *findByMember(MachineInstr *Member);
  const LayoutSite *findByMember(const MachineInstr *Member) const;

  /// Fail-closed consult. Fatals when \p Member is not in the table.
  LayoutSite *requireMember(MachineInstr *Member, StringRef Who);

  /// Null \p Member and drop it from ByMember. Call before
  /// eraseFromParent / eraseHardwareLoopSetup so collect cannot restore
  /// Rank/Template onto a recycled MI (MachineFunction.cpp:493
  /// InstructionRecycler).
  void dropMember(MachineInstr *Member);

  ArrayRef<LayoutSite> sites() const { return Sites; }
  MutableArrayRef<LayoutSite> sites() { return Sites; }
  bool empty() const { return Sites.empty(); }
};

} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNLAYOUTSITE_H
