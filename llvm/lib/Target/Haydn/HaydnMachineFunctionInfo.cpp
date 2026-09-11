//===-- HaydnMachineFunctionInfo.cpp - Haydn Machine Function Info --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

MachineFunctionInfo *HaydnMachineFunctionInfo::clone(
    BumpPtrAllocator &Allocator, MachineFunction &DestMF,
    const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
    const {
  auto *Copy = DestMF.cloneInfo<HaydnMachineFunctionInfo>(*this);
  // Transient MI* alternate-descriptor placement map must never cross
  // clone/outline: keys point into the source MF and would retain stale
  // setDesc/placement state (phase firewall).
  Copy->AltDescs.clear();
  // Same law for the inter-block DDG registry: the graphs' MBB/MI keys
  // belong to the source function.
  Copy->InterBlockRegistry.reset();
  // Per-function S1/S2 invocation count must not cross clone/outline.
  // Inheriting the source count skips or mis-fires first-S2 reopen
  // (counter == 2) on the dest. AIE clone is identity
  // (AIEMachineFunctionInfo.cpp:33-37); Haydn overlays per-function
  // lifecycle state (pipeline.md: S1 records are one-MF lifetime).
  Copy->PostRASchedInvocations = 0;
  // GR2.7/D1.40: the postcommit CFG identity snapshot binds to the source
  // MF's Finalize seat, not to the clone. A cloned/outlined MF that runs its
  // own Finalize stamps its own snapshot (write-once per function). The
  // identity tokens are NOT rebound through Src2DstMBB: the dest owns a
  // fresh stamp at its own first Finalize and never inherits an identity
  // set whose BasicBlock keys point into the source function.
  Copy->PostCommitCfgStamped = false;
  Copy->PostCommitCfg.LiveCount = 0;
  Copy->PostCommitCfg.BlockIDHighWater = 0;
  Copy->PostCommitCfg.NumberingEpoch = 0;
  Copy->PostCommitCfg.CreationHighWater = 0;
  Copy->PostCommitCfg.Tokens.clear();
  Copy->PostCommitCfg.SuccPositions.clear();
  // D1.102 / D1.150 latch→FI keys point into the source MF.
  Copy->HwLoopStackCounterByLatch.clear();
  for (const auto &KV : HwLoopStackCounterByLatch) {
    MachineBasicBlock *Src = const_cast<MachineBasicBlock *>(KV.first);
    auto It = Src2DstMBB.find(Src);
    if (It != Src2DstMBB.end() && It->second)
      Copy->HwLoopStackCounterByLatch[It->second] = KV.second;
  }
  Copy->HwLoopDemoteSaveByLatch.clear();
  for (const auto &KV : HwLoopDemoteSaveByLatch) {
    MachineBasicBlock *Src = const_cast<MachineBasicBlock *>(KV.first);
    auto It = Src2DstMBB.find(Src);
    if (It != Src2DstMBB.end() && It->second)
      Copy->HwLoopDemoteSaveByLatch[It->second] = KV.second;
  }
  return Copy;
}

HaydnMachineFunctionInfo::HaydnMachineFunctionInfo(const Function &F,
                                                   const TargetSubtargetInfo *STI)
    : UsesAGU(false), HasFP(false), VarArgsStackOffset(0) {
  // Propagate the immutable production ObjectEncodingProfile from the
  // subtarget. Reject any non-production profile so synthetic test families
  // cannot leak into a MachineFunction.
  // Haydn MFI is only constructed for Haydn subtargets. Avoid dyn_cast: the
  // subtarget type is not registered in LLVM's classof hierarchy.
  if (STI) {
    EncodingProfile =
        static_cast<const HaydnSubtarget *>(STI)->getObjectEncodingProfileID();
  } else {
    EncodingProfile = haydn::format::ObjectEncodingProfileID::E96;
  }
  if (!haydn::format::isProductionProfile(EncodingProfile))
    report_fatal_error(
        "Haydn MachineFunction requires the production ObjectEncodingProfile");
}

std::string HaydnMachineFunctionInfo::postCommitCfgCreationViolation(
    const MachineFunction &MF) const {
  if (!PostCommitCfgStamped)
    return {};
  const unsigned L1 = static_cast<unsigned>(MF.size());
  const unsigned L0 = PostCommitCfg.LiveCount;
  const unsigned H1 = MF.getNumBlockIDs();
  const unsigned H0 = PostCommitCfg.BlockIDHighWater;
  // L1 law (published growth): long-form promotion must be selected
  // pre-scheduler; postcommit CFG creation (BranchRelaxation
  // trampoline/RestoreBB/split arms) is refused. RenumberBlocks does not
  // change MF.size(), and the unused RestoreBB erase nets out, but the
  // always-created BranchBB leaves size >= L0+1 on any promotion.
  if (L1 > L0)
    return ("postcommit CFG creation: block count grew from the first-"
            "Finalize stamp " + std::to_string(L0) + " to " +
            std::to_string(L1) +
            " (BranchRelaxation trampoline/RestoreBB/split postcommit; "
            "long-form promotion must be selected pre-scheduler)");
  // L2 law (shrink): the postcommit CFG is immutable — MBB erasure is a
  // block reassignment/reflow and is refused exactly like creation. No
  // legitimate post-stamp eraser exists in the current seat graph: the only
  // erase source is insertIndirectBranch's unused RestoreBB, and that
  // callback already refuses outright when the wall is armed
  // (HaydnInstrInfo insertIndirectBranch).
  if (L1 < L0)
    return ("postcommit CFG shrink: block count fell from the first-Finalize "
            "stamp " +
            std::to_string(L0) + " to " + std::to_string(L1) +
            " (postcommit CFG is identity-frozen; block removal/reassignment "
            "must be normalized before the commit)");
  // L4 law (equal-count identity): compare the ordered token sequence.
  // Tokens are renumber-stable (BasicBlock identity + BBID, or CreationID
  // for null-BB/no-BBID, never MBB numbers/pointers), so a legal
  // RenumberBlocks cannot fire this law; erase+re-add or split-and-merge
  // at equal MF.size() changes the sequence. Null-BB/no-BBID replacement
  // no longer shares NoBBIDSentinel. Length is guaranteed equal here by
  // L1/L2 above.
  unsigned I = 0;
  for (const MachineBasicBlock &MBB : MF) {
    const auto &Stamped = PostCommitCfg.Tokens[I++];
    if (Stamped.first != MBB.getBasicBlock() ||
        Stamped.second != cfgIdentityToken(MBB))
      return ("postcommit CFG identity: MBB replacement at equal block "
              "count (layout position " +
              std::to_string(I - 1) +
              " changed identity after the first-Finalize stamp; equal-count "
              "erase+re-add / split-and-merge is refused — postcommit CFG is "
              "identity-frozen)");
  }
  // L3 law (numbering-slot slack, epoch-guarded): every post-stamp MBB
  // create or erase leaves a trace in the block-ID table — an erased MBB
  // nulls its slot (slack grows by one; net-zero on erase+re-add), and a
  // created-then-erased MBB appends a fresh slot. Measured as a slack DELTA
  // (H-L vs H0-L0), never absolute density, so sparse bb.N holes already
  // present at the stamp stay legal. The epoch guard keeps BranchRelaxation's
  // unconditional entry RenumberBlocks legal: it compacts null slots and
  // bumps the epoch, after which only the token laws remain enforceable
  // (that compaction is also why BR renumbers BEFORE its own mutations —
  // creations after the entry renumber re-grow the slack and still fire).
  if (MF.getBlockNumberEpoch() == PostCommitCfg.NumberingEpoch &&
      H1 - L1 != H0 - L0)
    return ("postcommit CFG numbering: block-ID slot slack changed from the "
            "first-Finalize stamp (" +
            std::to_string(H0 - L0) + " null/extra slots) to " +
            std::to_string(H1 - L1) +
            " (create-then-delete or erase+replace left a numbering trace; "
            "postcommit CFG is identity-frozen)");
  // L6 law (creation high-water): L3 is epoch-guarded so BR-entry
  // RenumberBlocks stays legal and retires the numbering-slot trace. A
  // later CreateMachineBasicBlock — including create-then-delete that
  // restores live count and tokens — is still visible as a monotone
  // CreationID high-water advance. Keyed on the HC#0 serial, never on
  // numbering slack/epoch, so the entry renumber cannot false-fire and
  // cannot launder a later create. Additive after L3; L1 already names
  // live growth. L4 already names equal-count identity replacement.
  const unsigned C1 = MF.getMBBCreationHighWater();
  const unsigned C0 = PostCommitCfg.CreationHighWater;
  if (C1 > C0)
    return ("postcommit CFG high-water: MBB creation serial advanced from "
            "the first-Finalize stamp " +
            std::to_string(C0) + " to " + std::to_string(C1) +
            " (CreateMachineBasicBlock after the stamp, including "
            "create-then-delete after RenumberBlocks, is refused — "
            "postcommit CFG is identity-frozen)");
  // L5 law (edge digest): block identity is proven unchanged by L1/L2/L4
  // above, so compare every MBB's successor list (digested as successor
  // LAYOUT POSITIONS — the same renumber-stable domain as the token
  // vector; successor order is preserved by add/remove/replace). Any
  // successor-digest mismatch is a hard freeze. Wave 4 H: product demote
  // refuses while stamped (SET/CFG untouched). GR2.10: no admitted-
  // transition exception and no consume-and-advance.
  I = 0;
  for (const MachineBasicBlock &MBB : MF) {
    const SmallVectorImpl<unsigned> &StampedSucc =
        PostCommitCfg.SuccPositions[I];
    bool Diverged = false;
    if (MBB.succ_size() != StampedSucc.size()) {
      Diverged = true;
    } else {
      auto StampedIt = StampedSucc.begin();
      for (const MachineBasicBlock *S : MBB.successors()) {
        if (cfgLayoutPosition(MF, *S) != *StampedIt++) {
          Diverged = true;
          break;
        }
      }
    }
    if (Diverged)
      return ("postcommit CFG edges: successor sequence of layout position " +
              std::to_string(I) +
              " changed after the first-Finalize stamp (edge-only mutation "
              "is refused — postcommit CFG is identity-frozen)");
    ++I;
  }
  return {};
}
