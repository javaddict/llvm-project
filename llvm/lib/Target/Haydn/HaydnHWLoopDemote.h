//===-- HaydnHWLoopDemote.h - Shared HWLOOP demote/erase helpers -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MI-level helpers shared by hardware-loop setup erase and software-loop
// demotion. Single owner for the helpers previously duplicated verbatim
// between formation (HaydnHardwareLoops) and pre-emit fixup
// (HaydnFixupHwLoops). The two exported entry points stay in
// HaydnHardwareLoops.h (llvm::eraseHardwareLoopSetup /
// llvm::demoteHardwareLoopToSoftware) and call through to these.
//
// Body-resolution law (do not weaken):
//   resolveBodyMBBCore resolves the LoopStart/SET body from CFG state only
//   (SET op1 MBB; PLE-carrying preheader successor; unique successor whose
//   latch PLE targets it). Formation uses core directly: a body reachable
//   only via layout order is incomplete retained state and must reject
//   fail-closed (HaydnHardwareLoops resolveRoleABody). Only pre-emit Fixup
//   may append a final-layout tail — at fixup layout is final and
//   BranchRelaxation may have severed the direct preheader->body edge into
//   a continue-trampoline — via resolveBodyMBBFixup, never via a second
//   copy in the Fixup TU.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNHWLOOPDEMOTE_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNHWLOOPDEMOTE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCRegister.h"

#include <cstdint>

namespace llvm {

class HaydnInstrInfo;
class HaydnSubtarget;
class LivePhysRegs;
class MachineFunction;
class TargetInstrInfo;
class TargetRegisterInfo;

namespace haydn {
namespace hwloop {

/// Blocks of the hardware loop body: Header, Latch, and every CFG
/// predecessor of Latch that can reach Latch without leaving the loop.
using LoopBlockSet = SmallPtrSet<const MachineBasicBlock *, 8>;

/// Body resolver passed to the exported demote/erase entry points so fixup
/// can extend the CFG-only core with its final-layout tail.
using ResolveBodyFn = MachineBasicBlock *(*)(MachineInstr &);

/// Product demote policy (default ON). Debug OFF refuses a soft-edge
/// install on a live body so callers fatal rather than erase-only
/// once-through (HexagonFixupHwLoops.cpp:fixupLoopInstrs skip overlay).
bool isHwLoopDemoteEnabled();

/// True iff \p BB (BUNDLE interiors included) writes the unpublished
/// HWLR CSR window. Product programs HWLR only through SET_HWLOOP;
/// haydnHwloopCsrAddr (HaydnPortModel.h) is the one 0x20-0x25 predicate.
/// Walks instrs() so a packed CSRW child is not missed after post-RA.
bool blockContainsUnpublishedHwlrCsr(const MachineBasicBlock &BB);

/// True iff any block in \p Blocks writes the unpublished HWLR window.
bool loopBlocksContainUnpublishedHwlrCsr(const LoopBlockSet &Blocks);

/// CB-162/CB-165 value-preserve placement. The demote's save/restore pair
/// exists only to return the value Prefer (the SET trip register) must
/// carry OUT of the loop, and only the demote's own latch-scratch window
/// can destroy that value:
///
///   * NoSave          — latch scratch != Prefer: nothing the demote
///                       installs touches Prefer; any save/restore would
///                       reload a stale value over the live exit value
///                       (the CB-165 miscompile: pr51581-2 c[N-1]=trip).
///   * PreheaderSave   — latch scratch == Prefer and the body does NOT
///                       redefine Prefer: Prefer carries the trip through
///                       untouched; save the trip in the preheader and
///                       reload it at the exit (the CB-162 shape).
///   * LatchEndSave    — latch scratch == Prefer and the body REDEFINES
///                       Prefer (MachinePipeliner assigns the loop-carried
///                       stage value to the trip physreg): the exit value
///                       is the body's final def, so the save must execute
///                       at latch end, just before the scratch window.
///
/// HasImm forms have no live Prefer to preserve (caller passes
/// PreferValid=false).
enum class HwLoopDemoteSaveKind {
  NoSave,
  PreheaderSave,
  LatchEndSave,
};

/// The pure placement decision for the value-preserve save. Inputs are the
/// resolved facts, not the MachineFunction, so this is the unit seam:
///   \p PreferIsLatchScratch  LatchScr == Prefer after the scratch probe;
///   \p PreferRedefinedInBody regClobberedNonCountdownIn(Prefer, blocks).
HwLoopDemoteSaveKind
demoteSavePlacement(bool PreferIsLatchScratch, bool PreferRedefinedInBody);

/// Convenience wrapper resolving PreferRedefinedInBody from the body
/// blocks (regClobberedNonCountdownIn is the authority).
HwLoopDemoteSaveKind demoteSavePlacement(Register Prefer, Register LatchScr,
                                         const LoopBlockSet &Blocks);

/// Stack-counter demote admission law (D1.19). Total, closed decision over
/// the (LatchScr validity, HasImm, PreheaderScr validity, Adj!=0,
/// LatchScr==Prefer) matrix for the demote arm that keeps the trip in a
/// scratch FI and reloads it each latch. Inputs are the resolved facts at
/// the single gate site (HaydnHardwareLoops demoteHardwareLoopToSoftware),
/// same seam style as demoteSavePlacement. The full matrix:
///
///   (a) !LatchScrValid                -> refuse. The latch LD32/SUBI32/ST32
///       window needs a spill-free non-R0 register; a spill bracket's home
///       aliases the counter FI itself (existing law).
///   (b) Adj==0 reg-trip (any LatchScr) -> admissible. The remaining kernel
///       trip IS the full trip N still carried by Prefer; the preheader
///       ST32 stores Prefer directly, no PreheaderScr needed (sound).
///   (c) Adj!=0, PreheaderScrValid, PreheaderScr != Prefer -> admissible.
///       ADDI PreheaderScr, Prefer, Adj then ST32 PreheaderScr stores the
///       remaining trip N+Adj (the cb166 stack arm; sound).
///   (d) Adj!=0, !PreheaderScrValid, LatchScr physical != Prefer ->
///       admissible via the copy fallback PreheaderScr = LatchScr. Claimed
///       sound today: LatchScr was probed dead at latch end against the
///       post-rewrite successors, and the preheader ADDI defines it before
///       the loop. (Adjacent residual: LatchScr is not probed dead AT the
///       preheader insert point; filed separately if an MIR confirms it.)
///   (e) Adj!=0, !PreheaderScrValid, LatchScr == Prefer -> REFUSE (the
///       D1.19 defect). The ADDI addend dest must never be Prefer (in-place
///       ADDI destroys the trip Prefer carries; AIE LC-vs-src law and
///       rematerializeAddImmForUse never Dest==Src), so with no other
///       PreheaderScr the store block would fall back to StoreSrc = Prefer
///       and ST32 the FULL trip N while the kernel must run Prefer+Adj =
///       N-S — the S peeled iterations re-execute. Exactly reachable when
///       the CB-162 latch fallback set LatchScr = Prefer (empty latch
///       probe, Prefer not in {R0,R13,R15}) and the Adj!=0 preheader probe
///       (which always excludes Prefer) plus the copy fallback (which
///       requires LatchScr != Prefer) both miss.
///   (f) HasImm without PreheaderScrValid -> refuse. The imm-trip
///       preheader window materializes the trip into PreheaderScr before
///       the ST32; no scratch means no sound materialize seat (existing
///       law; HasImm never has an Adj: SET_* already rematted the addend).
bool demoteStackCounterAdmissible(bool LatchScrValid, bool HasImm,
                                  bool PreheaderScrValid, bool AdjNonZero,
                                  bool LatchScrIsPrefer);

/// CFG-only body resolution shared by formation and fixup (see file law).
/// LoopStart: PLE-carrying preheader successor (single-BB), else the unique
/// preheader successor whose latch PLE targets it (multi-BB header).
/// No unique-successor-without-proof fallback (that successor may be exit).
MachineBasicBlock *resolveBodyMBBCore(MachineInstr &SetMI);

/// Fixup-only final-layout tail. CFG-only core first; then walk continue
/// trampolines (empty / NOP / LUI+ADDI+JALR / B) and accept the first
/// layout candidate that still proves a ZOL latch. A live next-MBB
/// without that proof is trampoline/exit — never invent it as the body
/// (Hexagon FixupHwLoops.cpp:97-148 converts or leaves LOOP). Formation
/// must not call this: a layout-only body is incomplete retained state.
MachineBasicBlock *resolveBodyMBBFixup(MachineInstr &SetMI);

/// Latch for a LoopStart header: PLE on Header targeting Header (single-BB),
/// or the unique non-preheader predecessor whose PLE targets Header.
/// Ambiguous / missing PLE returns nullptr. CFG only — never layout order.
MachineBasicBlock *resolveLoopStartLatch(MachineBasicBlock *Header,
                                         MachineBasicBlock *Preheader);

/// True iff \p BB is a BranchRelaxation continue-trampoline: empty or
/// only NOP / LUI / ADDI32_W / JALR* / B. Used by Fixup's layout tail
/// (walk past trampolines, then require a latch proof) and by demote
/// exit selection (skip Header-only trampoline successors).
bool isContinueTrampolineBlock(const MachineBasicBlock *BB);

/// Counted software back-edge after demote. Formation emits residual
/// BNEZ_W; late exact-commit rewrites it to the golden Format E BNEZ
/// member (HaydnGenFormatEMemberOpcodes.inc BNEZ_E2_E0_ALU0_I12 →
/// BNEZ). Hexagon FixupHwLoops.cpp:137-148 converts or leaves LOOP;
/// Haydn overlay is this BNEZ family, never a second loop opcode.
bool isSoftLatchBnezOpcode(unsigned Opc);

/// True iff \p MBB is a live block of \p MF (not erased / renumbered out).
/// SET operands can hold `%bb.-1` after CFG merges; those are not live.
bool isLiveMBB(const MachineFunction &MF, const MachineBasicBlock *MBB);

/// If \p BundleRoot has no remaining children after an unbundle, erase it.
/// HaydnFinalizeBundle wraps SET/LoopStart as singleton BUNDLEs; unbundling
/// the only child must not leave an empty BUNDLE shell that still carries
/// kill flags (verifier: "Using an undefined physical register").
void eraseEmptyBundleRoot(MachineInstr *BundleRoot);

/// Safe erase of a (possibly bundled) MI collected by pointer. Generic path
/// (PLE / terminators): drop empty BUNDLE shells only. SET setup erase uses
/// eraseSetMemberAndRecommitSiblings so coissued survivors keep a
/// transactionally recommitted product root (rebuilt operands/kills).
void eraseInstrSafe(MachineInstr *MI);

/// Top-level MI for layout edits when \p MI may be a BUNDLE interior.
/// Bundle-preserving splices/pads relative to this root never unbundle a
/// legal coissued SET cycle just to walk iterators.
MachineInstr &topLevelForLayout(MachineInstr &MI);

/// Late exact-commit member opcode for a logical opcode.
unsigned lateMemberOpcode(unsigned LogicalOpc);

/// Stamp the Format E singleton commit on one late member.
void finalizeExactLateSingleton(MachineInstr &MI);

/// After SET-member erase from a multi-member product cycle, re-exact-commit
/// surviving coissued siblings so the BUNDLE root is rebuilt (consolidated
/// defs/uses, kill flags, FormatID). Bare survivors would leave residual
/// real MIs for the late firewall and stale root operands.
void recommitSurvivingCycleMembers(ArrayRef<MachineInstr *> Keep,
                                   const HaydnInstrInfo &TII,
                                   const char *DebugPrefix);

/// SET/LoopStart-member erase that preserves coissued siblings as an exact
/// product cycle. Dissolves the old root, erases only the setup member, then
/// recommits remaining children so consolidated root operands match the
/// surviving membership.
void eraseSetMemberAndRecommitSiblings(MachineInstr &SetMI,
                                       const HaydnInstrInfo &TII,
                                       const char *DebugPrefix);

/// Build a late singleton with exact-commit member opcode (dest form).
MachineInstrBuilder buildExactLateDef(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator InsertPt,
                                      const DebugLoc &DL,
                                      const TargetInstrInfo &TII,
                                      unsigned LogicalOpc, Register Dest);

/// Build a late singleton with exact-commit member opcode (no dest).
MachineInstrBuilder buildExactLate(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator InsertPt,
                                   const DebugLoc &DL,
                                   const TargetInstrInfo &TII,
                                   unsigned LogicalOpc);

/// Collect the CFG loop blocks (Header, Latch, reverse-reachable interior).
void collectLoopBlocks(const MachineBasicBlock *Header,
                       const MachineBasicBlock *Latch,
                       const MachineBasicBlock *Preheader,
                       LoopBlockSet &Out);

/// True if any MI in \p Blocks mentions \p Reg (use or def).
bool regMentionedInBlocks(Register Reg, const LoopBlockSet &Blocks);

/// True if \p Reg is defined in \p Blocks by a non-countdown op (load dest,
/// move, etc.). Pure residual countdown (Reg+=-1 / Reg-=1) is allowed.
/// Regmask-only clobbers (body calls) are invisible here — counter ownership
/// is decided by isSoundDemoteCounter.
bool regClobberedNonCountdownIn(Register Reg, const LoopBlockSet &Blocks);

/// Counter-ownership law for a software-loop demote on \p Reg whose live
/// range is the loop blocks plus the preheader tail from \p PreheaderFrom.
/// The tail may contain ordinary setup-distance work after SET (SET is not
/// a scheduling boundary); any def/use of \p Reg there refuses the register.
/// ABI callee-saved: usable iff this function's prologue actually saves it
/// (CalleeSavedInfo — demote runs post-PEI, an unsaved CSR write is never
/// repaired and silently breaks our caller; saved ⇒ also call-safe).
/// Caller-saved: usable iff no call on the range clobbers it (regmask-based;
/// clobber = corrupted trip). Stack-counter demote is the sink on refusal.
bool isSoundDemoteCounter(MCPhysReg Reg, const LoopBlockSet &Blocks,
                          const MachineBasicBlock *Preheader,
                          MachineBasicBlock::const_iterator PreheaderFrom,
                          const MachineFunction &MF,
                          const TargetRegisterInfo &TRI);

/// True if \p MI is a residual countdown step of \p Reg: generic
/// HardwareLoops LoopDec, or a leftover Prefer+=-1 / Prefer-=1 whose
/// step is a proven ±1 immediate. Same-reg ADD32/SUB32 with a register
/// rhs (any Reg ± R2) is an unverifiable addend and must return false
/// so stripResidualCountdown cannot erase live compute.
bool isCountdownStepOf(const MachineInstr &MI, Register Reg);

/// Erase residual countdown steps of \p Reg from every block in \p Blocks
/// so demote's SUBI32+BNEZ_W is the sole decrement. Walk instrs for
/// BUNDLE interiors. Header-side leftovers on a multi-BB diamond are
/// the same countdown as a latch leftover.
void stripResidualCountdown(const LoopBlockSet &Blocks, Register Reg);

/// Fill \p Live with the one-block live-ins of \p MBB: addLiveOuts then
/// reverse stepBackward. Stored MBB live-in lists are stale this late
/// (BranchRelaxation split tails may be empty). Overlay of AIE
/// AIELiveRegs.cpp:37-52 without the function-wide worklist class.
void computeBlockLiveIns(LivePhysRegs &Live, const MachineBasicBlock &MBB);

/// True iff the computed live-ins of \p MBB contain \p Reg.
bool blockLiveInContains(const MachineBasicBlock &MBB, MCPhysReg Reg);

/// Pick a free GPR for soft-loop countdown at \p InsertPt in \p Preheader.
/// Returns invalid Register if none is free (caller must refuse erase-only
/// on a live body — fatal via recoverRangeOrOrder).
Register pickCounterReg(const LoopBlockSet &Blocks, Register Prefer,
                        const HaydnSubtarget &ST,
                        MachineBasicBlock &Preheader,
                        MachineBasicBlock::iterator InsertPt,
                        const char *DebugPrefix);

/// Materialize a trip count at the SET site as exact-committed late
/// singletons (MOVE32 copy, or XOR-zero + ADDI32_W for immediates).
void materializeTripCount(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator InsertPt, DebugLoc DL,
                          const HaydnInstrInfo &TII, Register Dst,
                          Register SrcReg, int64_t SrcImm, bool HasImm);

/// Emit one exact-committed singleton (dest form) and stamp Format E commit.
template <typename AddOpsFn>
MachineInstr *
emitExactLateDef(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
                 const DebugLoc &DL, const TargetInstrInfo &TII,
                 unsigned LogicalOpc, Register Dest, AddOpsFn AddOps) {
  MachineInstrBuilder MIB =
      buildExactLateDef(MBB, InsertPt, DL, TII, LogicalOpc, Dest);
  AddOps(MIB);
  finalizeExactLateSingleton(*MIB);
  return MIB;
}

/// Emit one exact-committed singleton (no dest) and stamp Format E commit.
template <typename AddOpsFn>
MachineInstr *
emitExactLate(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
              const DebugLoc &DL, const TargetInstrInfo &TII,
              unsigned LogicalOpc, AddOpsFn AddOps) {
  MachineInstrBuilder MIB = buildExactLate(MBB, InsertPt, DL, TII, LogicalOpc);
  AddOps(MIB);
  finalizeExactLateSingleton(*MIB);
  return MIB;
}

} // namespace hwloop
} // namespace haydn
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNHWLOOPDEMOTE_H
