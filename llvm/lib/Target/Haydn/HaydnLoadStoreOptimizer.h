//===-- HaydnLoadStoreOptimizer.h - Haydn Load/Store Opt. ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Experimental post-RA MachineFunctionPass (default OFF:
// haydn-enable-ldst-opt). Feature-test / lit opt-in only — NOT the product
// post-inc path. Product post-inc expansion lives in HaydnExpandPostIncEarly
// (default ON; -haydn-enable-expand-post-inc-early). Single-home contract.
//
// When enabled, optimizes load/store sequences:
//
// Redundant load elimination: when the same memory location is loaded
// multiple times without an intervening store, the redundant loads are
// replaced with register COPYs.
// Store-to-load forwarding: when a store is immediately followed by a
// load of the same location, the load is replaced with a COPY of the
// stored value register.
// Post-increment addressing (experimental form): detects LD32/ST32 +
// ADDI32 base-update and folds into *_POST_INC pseudos for later
// expansion by ExpandPostIncEarly. Not a dual product path.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNLOADSTOREOPTIMIZER_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNLOADSTOREOPTIMIZER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class MachineInstr;
class MachineBasicBlock;
class HaydnInstrInfo;
class TargetRegisterInfo;

// Experimental (default OFF): redundant-load elim, store-to-load forward
// and experimental post-inc pseudo formation. Product post-inc expansion is
// HaydnExpandPostIncEarly — this pass is feature-test opt-in only.
class HaydnLoadStoreOptimizer : public MachineFunctionPass {
public:
  static char ID;

  HaydnLoadStoreOptimizer();

  StringRef getPassName() const override {
    return "Haydn Load/Store Optimizer";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  const HaydnInstrInfo *HII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;

  // Tracks a known memory content: which register holds the current value
  // the access width in bytes (used for same-base overlap invalidation), and
  // which instruction last loaded/stored it.
  struct MemContentInfo {
    // The register that holds the value at this memory location.
    Register ContentReg;
    // Access width in bytes (1/2/4/8). Used to detect overlapping accesses
    // to the same base register when a later store partially or fully
    // overwrites a previously tracked location. See invalidateOverlappingStores.
    uint8_t Size = 0;
    // The instruction that established this content.
    MachineInstr *SourceMI = nullptr;
  };

  // Key for identifying a memory location: base register + offset.
  struct MemLoc {
    Register BaseReg;
    int64_t Offset;

    bool operator==(const MemLoc &Other) const {
      return BaseReg == Other.BaseReg && Offset == Other.Offset;
    }
  };

  // DenseMapInfo for MemLoc.
  struct MemLocDenseMapInfo {
    static inline MemLoc getEmptyKey() {
      return {Register(~0u), INT64_MAX};
    }
    static inline MemLoc getTombstoneKey() {
      return {Register(~0u - 1), INT64_MAX - 1};
    }
    static unsigned getHashValue(const MemLoc &Loc) {
      return hash_combine(Loc.BaseReg.id(), Loc.Offset);
    }
    static bool isEqual(const MemLoc &LHS, const MemLoc &RHS) {
      return LHS == RHS;
    }
  };

  // Map from memory location to the register holding its current value.
  using MemContentMap =
      DenseMap<MemLoc, MemContentInfo, MemLocDenseMapInfo>;

  // Process a single basic block for redundancy elimination and forwarding.
  bool optimizeBlock(MachineBasicBlock &MBB);

  // Process a single basic block for post-increment addressing conversion.
  // Detects sequences like:
  // LD32 rt, base, offset followed by ADDI32 base, base, stride
  // and converts them to LD32_POST_INC rt, base, stride+offset.
  bool optimizePostIncBlock(MachineBasicBlock &MBB);

  // Invalidate all tracked memory locations that could be affected by a
  // call, an unknown store, or a register clobber.
  void invalidateAll(MemContentMap &KnownContent);

  // Invalidate entries whose base register is clobbered by the given
  // instruction.
  void invalidateClobberedRegs(MachineInstr &MI, MemContentMap &KnownContent);

  // Invalidate entries whose tracked address range overlaps the given store's
  // address range, i.e. same base register AND
  // [EntryOff, EntryOff+EntrySize) intersects [NewOff, NewOff+NewSize).
  // This is the size-aware same-base overlap check (no alias-analysis
  // dependency) that prevents stale store-to-load forwarding when an
  // overlapping store partially or fully overwrites a tracked location.
  // `\p NewMI` must be a trackable store; returns the count of dropped entries.
  unsigned invalidateOverlappingStores(MachineInstr &NewMI,
                                       MemContentMap &KnownContent);

  // Try to get the base register and offset from a load/store instruction.
  // Returns true on success.
  bool getLoadStoreAddress(const MachineInstr &MI, Register &BaseReg,
                           int64_t &Offset) const;

  // Check whether MI is a load instruction we can optimize.
  bool isLoad(const MachineInstr &MI) const;

  // Check whether MI is a store instruction we can track.
  bool isStore(const MachineInstr &MI) const;
};

// Factory function for the pass.
FunctionPass *createHaydnLoadStoreOptimizerPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNLOADSTOREOPTIMIZER_H
