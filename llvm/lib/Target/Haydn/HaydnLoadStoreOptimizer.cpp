//===-- HaydnLoadStoreOptimizer.cpp - Haydn Load/Store Opt. ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Experimental implementation (default OFF: -haydn-enable-ldst-opt).
// Feature-test / lit opt-in only. Product post-inc path is
// HaydnExpandPostIncEarly (default ON). Single-home contract: form-via
// this-pass is not a dual product path.
//
// When enabled, optimizes load/store sequences for the Haydn VLIW DSP:
//
// 1. Post-increment addressing formation (Phase 1, experimental):
// Detects patterns like:
// LD32 rt, base, offset followed by ADDI32 base, base, stride
// and folds them into a LD32_POST_INC pseudo instruction.
// Similarly for ST32, LD64_S1, ST64. Pseudos are lowered by the product
// ExpandPostIncEarly pass when that is also enabled.
//
// 2. Redundant load elimination (Phase 2):
// If the same memory address is loaded repeatedly without intervening
// stores, redundant loads are replaced with COPY instructions.
//
// 3. Store-to-load forwarding (Phase 2):
// If a store is followed by a load from the same address (with no
// intervening store or call), the load is replaced with a COPY
// from the register that was stored.
//
// Runs post-RA in addPreSched2 (before ExpandPostIncEarly / packetizer).
// Single basic block only (no cross-BB analysis).
//
//===----------------------------------------------------------------------===//

#include "HaydnLoadStoreOptimizer.h"
#include "Haydn.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "haydn-ldst-opt"

// Phase 2 (redundant-load-elim + store-to-load-forward) has an OPEN
// "forward-from-killed" correctness bug (yarpgen seed6). Default OFF:
// the pass fires only ~75× across the whole CoreMark+HiFi corpus (+0.013%
//text cost to disable — within noise), 52/75 being the buggy store-to-load
// forward. Re-enable here only after is root-caused and fixed with
// regression tests. Phase 1 (post-inc formation) is correctness-safe and always
// runs; it is NOT affected by this flag.
static llvm::cl::opt<bool> EnablePhase2(
    "haydn-ldst-opt-phase2",
    llvm::cl::desc("Haydn LoadStoreOpt: enable Phase 2 (redundant-load-elim + "
                   "store-to-load forwarding). Default OFF : the open"
                   "forward-from-killed bug makes Phase 2 unsafe; cost of"
                   "disabling is +0.013% .text."),
    llvm::cl::init(false), llvm::cl::Hidden);

using namespace llvm;

STATISTIC(NumRedundantLoadsEliminated,
          "Number of redundant loads eliminated");
STATISTIC(NumStoresForwarded,
          "Number of store-to-load forwardings performed");
STATISTIC(NumPostIncFormed,
          "Number of post-increment addressing patterns formed");

char HaydnLoadStoreOptimizer::ID = 0;

HaydnLoadStoreOptimizer::HaydnLoadStoreOptimizer()
    : MachineFunctionPass(ID) {}

//===----------------------------------------------------------------------===//
// Phase 1: Post-increment addressing mode formation
//===----------------------------------------------------------------------===//

// Check if an ADDI32 instruction is a simple base-register increment that
// can be folded into a preceding load/store. Specifically, it must be:
// ADDI32 BaseReg, BaseReg, Imm (i.e., rd == rs)
// Returns the stride value if foldable, or 0 otherwise.
static int64_t getAddiStrideIfFoldable(const MachineInstr &MI,
                                       Register BaseReg) {
  if (MI.getOpcode() != Haydn::ADDI32 && MI.getOpcode() != Haydn::ADDI32_W)
    return 0;

  // ADDI32 rd, rs, imm -> operands: 0=rd(def), 1=rs(use), 2=imm
  if (MI.getNumExplicitOperands() != 3)
    return 0;

  const MachineOperand &DefOp = MI.getOperand(0);
  const MachineOperand &SrcOp = MI.getOperand(1);
  const MachineOperand &ImmOp = MI.getOperand(2);

  if (!DefOp.isReg() || !SrcOp.isReg() || !ImmOp.isImm())
    return 0;

  // Must be: rd = rs + imm where rd == rs == BaseReg
  if (DefOp.getReg() != BaseReg || SrcOp.getReg() != BaseReg)
    return 0;

  return ImmOp.getImm();
}

// Detect ADD32 BaseReg, BaseReg, StrideReg (register-stride add).
// Returns the stride register if foldable, or invalid Register otherwise.
static Register getAdd32StrideRegIfFoldable(const MachineInstr &MI,
                                             Register BaseReg) {
  if (MI.getOpcode() != Haydn::ADD32)
    return Register();

  // ADD32 rd, rs1, rs2 -> operands: 0=rd(def), 1=rs1(use), 2=rs2(use)
  if (MI.getNumExplicitOperands() != 3)
    return Register();

  const MachineOperand &DefOp = MI.getOperand(0);
  const MachineOperand &Src1Op = MI.getOperand(1);
  const MachineOperand &Src2Op = MI.getOperand(2);

  if (!DefOp.isReg() || !Src1Op.isReg() || !Src2Op.isReg())
    return Register();

  // Must be: rd = rs1 + rs2 where rd == rs1 == BaseReg
  if (DefOp.getReg() != BaseReg || Src1Op.getReg() != BaseReg)
    return Register();

  return Src2Op.getReg();
}

// Try to convert a load/store + ADDI32 sequence into a post-increment pseudo.
// Returns true if the conversion was performed.
static bool tryFormPostInc(MachineInstr &MemMI, MachineInstr &AddMI,
                           MachineBasicBlock &MBB,
                           const HaydnInstrInfo *HII,
                           const TargetRegisterInfo *TRI) {
  unsigned MemOpc = MemMI.getOpcode();

  // Determine the post-increment pseudo opcode based on the memory instruction.
  unsigned PostIncOpc = 0;
  bool IsLoad = true;

  switch (MemOpc) {
  default:
    return false;
  case Haydn::LD32:
    PostIncOpc = Haydn::LD32_POST_INC;
    IsLoad = true;
    break;
  case Haydn::ST32:
    PostIncOpc = Haydn::ST32_POST_INC;
    IsLoad = false;
    break;
  case Haydn::LD64:
    PostIncOpc = Haydn::LD64_POST_INC;
    IsLoad = true;
    break;
  case Haydn::ST64:
    PostIncOpc = Haydn::ST64_POST_INC;
    IsLoad = false;
    break;
  }

  // Get the base register and the existing displacement from the load/store.
  // Loads: $rt(def), $base(use), $offset(use)
  // Stores: $rt(use), $base(use), $offset(use)
  const MachineOperand &BaseOp = MemMI.getOperand(1);
  if (!BaseOp.isReg())
    return false;

  Register BaseReg = BaseOp.getReg();

  // Carry the memory op's existing displacement into the post-inc pseudo so
  // it is preserved through expansion. The displacement is the offset
  // of the original LD32/ST32/LD64/ST64 — if a pre-RA fold produced a
  // non-zero offset (e.g. the offset-4 store of a cross-bank fold), the
  // post-inc must access [BaseReg + Offset] before applying the stride.
  const MachineOperand &OffOp = MemMI.getOperand(2);
  if (!OffOp.isImm())
    return false;
  int64_t Offset = OffOp.getImm();

  // Check that the ADDI32 is a foldable base-register update.
  int64_t Stride = getAddiStrideIfFoldable(AddMI, BaseReg);

  // If ADDI32 didn't match, try ADD32 (register-stride) for D_LDW_POST_REG.
  Register StrideReg;
  bool IsRegStride = false;
  if (Stride == 0) {
    StrideReg = getAdd32StrideRegIfFoldable(AddMI, BaseReg);
    if (!StrideReg.isValid())
      return false;
    IsRegStride = true;
    // Use the _REG post-inc forms for register stride.
    switch (MemOpc) {
    case Haydn::LD64:
      PostIncOpc = Haydn::D_LDW_POST_REG; // fused load+reg-stride
      IsLoad = true;
      break;
    default:
      return false; // only LD64 reg-stride supported for now
    }
    // the D_LDW_POST_REG encoding has NO displacement field — only
    // rs + rsd2 + variant selector + type + rt. Folding a load with a
    // non-zero displacement would silently drop the offset (the imm-offset
    // D_LDW_POST_IMM form carries offset via the imm8 field; the reg form
    // does not). Reject the fold if the original load had a non-zero
    // displacement. Latent before v2 because the candidate scan broke
    // on ADD32; now reachable, so guard it.
    if (Offset != 0)
      return false;
  }

  if (Stride == 0 && !IsRegStride)
    return false;

  // Verify that no instruction between the load/store and the ADDI32
  // clobbers the base register or the loaded/stored value register.
  // Also verify that the ADDI32's result is not used by any instruction
  // between the MemMI and AddMI (since the post-inc updates the base
  // *after* the memory access).
  //
  // For correctness, we need:
  // 1. No instruction between MemMI and AddMI clobbers BaseReg
  // 2. No instruction between MemMI and AddMI uses the *updated* BaseReg
  // (the ADDI32's def)
  // 3. The ADDI32 has no other uses besides its own def (i.e., it's dead
  // after itself except for its def, or the base is used later)
  //
  // Since the ADDI32 writes BaseReg, we need to verify that between MemMI
  // and AddMI, no instruction reads or writes BaseReg.
  auto It = MemMI.getIterator();
  ++It;
  auto EndIt = AddMI.getIterator();
  for (; It != EndIt; ++It) {
    if (It->modifiesRegister(BaseReg, TRI) ||
        It->readsRegister(BaseReg, TRI))
      return false;
    // If the intervening instruction is a memory access using the same base
    // we cannot fold — the post-increment would change the base too early.
    if (It->mayLoadOrStore() && It->readsRegister(BaseReg, TRI))
      return false;
  }

  // For loads: verify that the loaded data register (operand 0) is not
  // the same as BaseReg (otherwise the post-increment would clobber the
  // loaded value's register).
  if (IsLoad) {
    Register DataReg = MemMI.getOperand(0).getReg();
    if (DataReg == BaseReg)
      return false;
  }

  // Check that the stride fits in simm16 (the pseudo stores it as an
  // immediate, and the expansion will emit ADDI32 with this value).
  if (!isInt<16>(Stride))
    return false;

  // All checks passed — build the post-increment pseudo instruction.
  DebugLoc DL = MemMI.getDebugLoc();

  if (IsLoad) {
    Register DataReg = MemMI.getOperand(0).getReg();
    if (IsRegStride) {
      // D_LDW_POST_REG $rtd(def), $rs_wb(def), $rs(use), $rs2(use)
      // This is a tied-operand instruction (Constraints = "$rs = $rs_wb").
      BuildMI(MBB, MemMI, DL, HII->get(PostIncOpc))
          .addReg(DataReg, RegState::Define)
          .addReg(BaseReg, RegState::Define)
          .addReg(BaseReg)
          .addReg(StrideReg);
    } else {
      // Loads: LD32_POST_INC $rt(def), $base, $stride, $offset
      BuildMI(MBB, MemMI, DL, HII->get(PostIncOpc), DataReg)
          .addReg(BaseReg)
          .addImm(Stride)
          .addImm(Offset);
    }
  } else {
    // Stores: ST32_POST_INC $rt, $base, $stride, $offset
    Register DataReg = MemMI.getOperand(0).getReg();
    BuildMI(MBB, MemMI, DL, HII->get(PostIncOpc))
        .addReg(DataReg)
        .addReg(BaseReg)
        .addImm(Stride)
        .addImm(Offset);
  }

  LLVM_DEBUG(dbgs() << "HaydnLdStOpt: Formed post-increment from:\n  ";
             MemMI.dump(); dbgs() << "  + \n"; AddMI.dump();
             dbgs() << "  -> stride=" << Stride << " offset=" << Offset
                     << "\n");

  // Remove the original instructions.
  MemMI.eraseFromParent();
  AddMI.eraseFromParent();

  ++NumPostIncFormed;
  return true;
}

bool HaydnLoadStoreOptimizer::optimizePostIncBlock(MachineBasicBlock &MBB) {
  bool Changed = false;

  // We iterate backwards to avoid iterator invalidation when removing.
  // For each load/store, look ahead to see if the next use of its base
  // register is an ADDI32 that updates it.
  //
  // Strategy: collect candidate load/store instructions, then for each one
  // scan forward looking for a foldable ADDI32.
  SmallVector<MachineInstr *, 8> Candidates;

  for (MachineInstr &MI : MBB) {
    if (MI.isDebugInstr() || MI.isCFIInstruction() || MI.isPosition())
      continue;

    unsigned Opc = MI.getOpcode();
    // Logical loads/stores only. tryFormPostInc handles LD32/ST32
    // LD64/ST64.
    if (Opc == Haydn::LD32 || Opc == Haydn::ST32 || Opc == Haydn::LD64 ||
        Opc == Haydn::ST64) {
      // Only consider loads/stores with a register base and immediate offset.
      if (MI.getNumExplicitOperands() >= 3 &&
          MI.getOperand(1).isReg() && MI.getOperand(2).isImm()) {
        Candidates.push_back(&MI);
      }
    }
  }

  // Process candidates. We iterate in reverse order so that earlier
  // instructions are processed after later ones, which avoids issues
  // when multiple candidates might interact.
  for (auto It = Candidates.rbegin(); It != Candidates.rend(); ++It) {
    MachineInstr *MemMI = *It;

    // The instruction may have been erased by a prior iteration.
    if (MemMI->isDebugInstr() || MemMI->getParent() == nullptr)
      continue;

    Register BaseReg = MemMI->getOperand(1).getReg();

    // Scan forward from the load/store to find a foldable ADDI32/ADD32.
    // Limit the search window to avoid quadratic behavior.
    // bumped from 8 to 16 for FFT/FIR where the post-inc add sits
    // 10+ instructions after the load.
    // bkfir32x32 MAC loop: 4 CB loads + 2 coef LD64 + 16 FF2MULA between
    // the last coef load and `ADDI32 ptr, 16` (~20 real insns). ScanLimit
    // 16 stopped one short of the add, so the coef pointer stayed as a
    // free ADDI32 IV. That ADDI won SMS NodeSet ASAP-tiebreak (highest
    // NodeNum) and scheduled the pointer PHI before the loads → es>ls on
    // LD64_S1 (Found=0). 32 covers the 16-MAC FIR shape; tryFormPostInc
    // intervening-use checks keep the fold correct.
    MachineBasicBlock::iterator ScanIt = std::next(MemMI->getIterator());
    MachineBasicBlock::iterator EndIt = MemMI->getParent()->end();
    unsigned ScanLimit = 32;

    while (ScanIt != EndIt && ScanLimit > 0) {
      MachineInstr &Candidate = *ScanIt;
      ++ScanIt;
      --ScanLimit;

      if (Candidate.isDebugInstr() || Candidate.isCFIInstruction() ||
          Candidate.isPosition()) {
        continue;
      }

      // If another instruction defines the base register before the ADDI32
      // we can't fold (the ADDI32 would update a stale base).
      // ADD32 BaseReg, BaseReg, StrideReg is the register-stride fold
      // target — tryFormPostInc's fallback consumes it and emits
      // D_LDW_POST_REG. An ADD32 that does NOT have rd==rs1==BaseReg is an
      // unrelated add (e.g. accumulator update) and must NOT break the scan
      // otherwise the imm-stride fold that follows is missed. Only treat
      // the ADD32 as a scan terminator when it actually targets BaseReg.
      if (Candidate.modifiesRegister(BaseReg, TRI) &&
          Candidate.getOpcode() != Haydn::ADDI32 &&
          Candidate.getOpcode() != Haydn::ADDI32_W &&
          !(Candidate.getOpcode() == Haydn::ADD32 &&
            getAdd32StrideRegIfFoldable(Candidate, BaseReg).isValid())) {
        break;
      }

      // If we find a call, we can't fold across it.
      if (Candidate.isCall())
        break;

      // If we find a foldable ADDI32 (imm stride) or ADD32 (register stride
      // on BaseReg), try to form the post-increment. tryFormPostInc handles
      // both: ADDI32 maps to D_LDW_POST_IMM / S_LW_POST_IMM, ADD32 maps to
      // D_LDW_POST_REG. Skip a non-foldable ADD32 (rd != BaseReg) — let the
      // scan continue past it to find the real base update.
      if (Candidate.getOpcode() == Haydn::ADDI32 ||
          Candidate.getOpcode() == Haydn::ADDI32_W ||
          (Candidate.getOpcode() == Haydn::ADD32 &&
           getAdd32StrideRegIfFoldable(Candidate, BaseReg).isValid())) {
        if (tryFormPostInc(*MemMI, Candidate, MBB, HII, TRI)) {
          Changed = true;
        }
        break;
      }

      // If we encounter another load/store to the same base, don't fold
      // the ADDI32 update might be for this later access instead.
      if (Candidate.mayLoadOrStore() &&
          Candidate.readsRegister(BaseReg, TRI)) {
        break;
      }
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// Phase 2: Redundant load elimination and store-to-load forwarding
//===----------------------------------------------------------------------===//

// Helper: determine whether an instruction is a load we can optimize.
bool HaydnLoadStoreOptimizer::isLoad(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  return Opc == Haydn::LD32 || Opc == Haydn::LD16 || Opc == Haydn::LD8 ||
         Opc == Haydn::LDU16 || Opc == Haydn::LDU8 || Opc == Haydn::LD64;
}

// Helper: determine whether an instruction is a store we can track.
bool HaydnLoadStoreOptimizer::isStore(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  return Opc == Haydn::ST32 || Opc == Haydn::ST16 || Opc == Haydn::ST8 ||
         Opc == Haydn::ST64;
}

// Return the access width in bytes for a tracked load/store opcode, or 0 if
// the opcode is not one we track. Sizes follow the opcode widths used by the
// `SizesMatch` forwarding rules above:
// ST8/LD8/LDU8 -> 1
// ST16/LD16/LDU16 -> 2
// ST32/LD32 -> 4
// ST64/LD64 -> 8
static uint8_t getMemAccessSize(unsigned Opc) {
  switch (Opc) {
  case Haydn::ST8:
  case Haydn::LD8:
  case Haydn::LDU8:
    return 1;
  case Haydn::ST16:
  case Haydn::LD16:
  case Haydn::LDU16:
    return 2;
  case Haydn::ST32:
  case Haydn::LD32:
    return 4;
  case Haydn::ST64:
  case Haydn::LD64:
    return 8;
  default:
    return 0;
  }
}

// Extract base register and offset from a Haydn load/store instruction.
bool HaydnLoadStoreOptimizer::getLoadStoreAddress(const MachineInstr &MI,
                                                   Register &BaseReg,
                                                   int64_t &Offset) const {
  if (MI.getNumExplicitOperands() < 3)
    return false;

  const MachineOperand &BaseOp = MI.getOperand(1);
  const MachineOperand &OffOp = MI.getOperand(2);

  if (!BaseOp.isReg() || !OffOp.isImm())
    return false;

  BaseReg = BaseOp.getReg();
  Offset = OffOp.getImm();
  return BaseReg.isValid();
}

void HaydnLoadStoreOptimizer::invalidateAll(MemContentMap &KnownContent) {
  KnownContent.clear();
}

void HaydnLoadStoreOptimizer::invalidateClobberedRegs(
    MachineInstr &MI, MemContentMap &KnownContent) {
  SmallVector<MemLoc, 4> ToErase;
  for (auto &[Loc, Info] : KnownContent) {
    // Drop the entry if its address base register is clobbered...
    if (MI.modifiesRegister(Loc.BaseReg, TRI)) {
      ToErase.push_back(Loc);
      continue;
    }
    // or if the register holding the tracked stored value is clobbered.
    // Store-to-load forwarding reuses ContentReg as the source of a COPY, so
    // if it has been redefined since the tracking store/load, the forwarded
    // value would read a stale (potentially undefined) register. This was
    // latent while the blanket SFR Defs serialized scalar ALU ops;
    // removed that chain, letting regalloc reuse the stored register between
    // the store and a later reload, which exposed this missing invalidation.
    if (Info.ContentReg.isValid() &&
        MI.modifiesRegister(Info.ContentReg, TRI))
      ToErase.push_back(Loc);
  }
  for (const MemLoc &Loc : ToErase)
    KnownContent.erase(Loc);
}

unsigned HaydnLoadStoreOptimizer::invalidateOverlappingStores(
    MachineInstr &NewMI, MemContentMap &KnownContent) {
  Register NewBaseReg;
  int64_t NewOff = 0;
  if (!getLoadStoreAddress(NewMI, NewBaseReg, NewOff))
    return 0;

  uint8_t NewSize = getMemAccessSize(NewMI.getOpcode());
  if (NewSize == 0)
    return 0;

  SmallVector<MemLoc, 4> ToErase;
  for (auto &[EntryLoc, Info] : KnownContent) {
    // Only same-base entries can alias in our offset-based tracking model.
    if (EntryLoc.BaseReg != NewBaseReg)
      continue;
    // Size-aware half-open interval overlap test:
    // [EntryOff, EntryOff + EntrySize) vs [NewOff, NewOff + NewSize)
    uint8_t EntrySize = Info.Size > 0 ? Info.Size : NewSize;
    int64_t EntryLo = EntryLoc.Offset;
    int64_t EntryHi = EntryLo + EntrySize;
    int64_t NewHi = NewOff + NewSize;
    if (NewOff < EntryHi && EntryLo < NewHi)
      ToErase.push_back(EntryLoc);
  }

  unsigned Dropped = ToErase.size();
  for (const MemLoc &Loc : ToErase)
    KnownContent.erase(Loc);
  return Dropped;
}

bool HaydnLoadStoreOptimizer::optimizeBlock(MachineBasicBlock &MBB) {
  bool Changed = false;
  MemContentMap KnownContent;

  for (MachineInstr &MI : make_early_inc_range(MBB)) {
    if (MI.isDebugInstr() || MI.isCFIInstruction() || MI.isPosition())
      continue;

    if (MI.isCall()) {
      invalidateAll(KnownContent);
      continue;
    }

    if (MI.hasUnmodeledSideEffects()) {
      invalidateAll(KnownContent);
      continue;
    }

    if (MI.mayStore() && !isStore(MI)) {
      invalidateAll(KnownContent);
      continue;
    }

    invalidateClobberedRegs(MI, KnownContent);

    // Handle loads
    if (isLoad(MI)) {
      Register BaseReg;
      int64_t Offset;
      if (!getLoadStoreAddress(MI, BaseReg, Offset)) {
        invalidateAll(KnownContent);
        continue;
      }

      MemLoc Loc{BaseReg, Offset};
      auto It = KnownContent.find(Loc);

      if (It != KnownContent.end()) {
        Register PrevReg = It->second.ContentReg;
        Register DstReg = MI.getOperand(0).getReg();

        unsigned CurOpc = MI.getOpcode();
        MachineInstr *PrevMI = It->second.SourceMI;
        unsigned PrevOpc = PrevMI->getOpcode();

        bool SizesMatch = false;
        if ((CurOpc == Haydn::LD32 && PrevOpc == Haydn::LD32) ||
            (CurOpc == Haydn::LD32 && PrevOpc == Haydn::ST32)) {
          SizesMatch = true;
        } else if ((CurOpc == Haydn::LD64 && PrevOpc == Haydn::LD64) ||
                   (CurOpc == Haydn::LD64 && PrevOpc == Haydn::ST64)) {
          SizesMatch = true;
        } else if ((CurOpc == Haydn::LD16 && PrevOpc == Haydn::LD16) ||
                   (CurOpc == Haydn::LD16 && PrevOpc == Haydn::ST16)) {
          SizesMatch = true;
        } else if ((CurOpc == Haydn::LD8 && PrevOpc == Haydn::LD8) ||
                   (CurOpc == Haydn::LD8 && PrevOpc == Haydn::ST8)) {
          SizesMatch = true;
        }

        if (SizesMatch && DstReg != PrevReg) {
          LLVM_DEBUG(dbgs() << "HaydnLdStOpt: Eliminating redundant load: ";
                     MI.dump(); dbgs() << "  Replacing with COPY from "
                                       << printReg(PrevReg, TRI) << "\n");

          if (PrevOpc == Haydn::ST32 || PrevOpc == Haydn::ST64 ||
              PrevOpc == Haydn::ST16 || PrevOpc == Haydn::ST8) {
            PrevMI->getOperand(0).setIsKill(false);
          }

          const DebugLoc &DL = MI.getDebugLoc();
          if (Haydn::DR64RegClass.contains(DstReg, PrevReg)) {
            BuildMI(MBB, MI, DL, HII->get(Haydn::OR64), DstReg)
                .addReg(PrevReg, getKillRegState(false))
                .addReg(PrevReg, getKillRegState(false));
          } else {
            BuildMI(MBB, MI, DL, HII->get(Haydn::ADD32), DstReg)
                .addReg(PrevReg, getKillRegState(false))
                .addReg(Haydn::R0);
          }

          MI.eraseFromParent();
          if (PrevOpc == Haydn::ST32 || PrevOpc == Haydn::ST64 ||
              PrevOpc == Haydn::ST16 || PrevOpc == Haydn::ST8)
            NumStoresForwarded++;
          else
            NumRedundantLoadsEliminated++;
          Changed = true;
          continue;
        }
      }

      Register DstReg = MI.getOperand(0).getReg();
      uint8_t Size = getMemAccessSize(MI.getOpcode());
      KnownContent[Loc] = {DstReg, Size, &MI};
      continue;
    }

    // Handle stores
    if (isStore(MI)) {
      Register BaseReg;
      int64_t Offset;
      if (!getLoadStoreAddress(MI, BaseReg, Offset)) {
        invalidateAll(KnownContent);
        continue;
      }

      // Drop any tracked entries (including the exact same offset) that the new
      // store's byte range overlaps. This replaces the previous dead `ToErase`
      // loop, which built an empty list (no-op) and so never invalidated
      // overlapping/aliasing stores — causing wrong store-to-load forwarding.
      // same-base, size-aware interval-overlap test, no AA dependency.
      invalidateOverlappingStores(MI, KnownContent);

      MemLoc Loc{BaseReg, Offset};
      Register StoredReg = MI.getOperand(0).getReg();
      uint8_t Size = getMemAccessSize(MI.getOpcode());
      KnownContent[Loc] = {StoredReg, Size, &MI};

      continue;
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// Top-level driver.
//===----------------------------------------------------------------------===//
bool HaydnLoadStoreOptimizer::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  const HaydnSubtarget &ST = MF.getSubtarget<HaydnSubtarget>();
  HII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();

  bool Changed = false;

  // Phase 1: Post-increment addressing mode formation.
  for (MachineBasicBlock &MBB : MF)
    Changed |= optimizePostIncBlock(MBB);

  // Phase 2: Redundant load elimination and store-to-load forwarding.
  // Gated by -haydn-ldst-opt-phase2 (default ON) due to the open
  // forward-from-killed correctness bug. Disable to A/B-measure or to
  // ship a safe-by-default backend while the bug is unresolved.
  if (EnablePhase2) {
    for (MachineBasicBlock &MBB : MF)
      Changed |= optimizeBlock(MBB);
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// Public API — pass creation and initialization.
//===----------------------------------------------------------------------===//

FunctionPass *llvm::createHaydnLoadStoreOptimizerPass() {
  return new HaydnLoadStoreOptimizer();
}

INITIALIZE_PASS(HaydnLoadStoreOptimizer, DEBUG_TYPE,
                "Haydn Load/Store Optimizer", false, false)
