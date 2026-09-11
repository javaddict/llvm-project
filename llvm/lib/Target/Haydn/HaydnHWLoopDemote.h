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
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCRegister.h"

#include <cstdint>
#include <memory>
#include <string>

namespace llvm {

class AAResults;
class HaydnInstrInfo;
class HaydnSubtarget;
class MachineFunction;
class TargetInstrInfo;
class TargetRegisterInfo;

namespace haydn {
namespace hwloop {

/// Blocks of the hardware loop body: Header, Latch, reverse-reachable
/// interior, and early-exit-only blocks forward-reachable from Header
/// that are not the unique designated Exit (D1.101). Stored MBB live-ins
/// are not this set.
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

/// Dedicated CB-162 demote save home (D1.150 / D1.154). Returns \p SaveFI
/// iff it is reserved and disjoint from PostRAScratchFI,
/// BranchRelaxationScratchFI, and every stack-counter pool member;
/// otherwise -1. Never falls back to PostRA: disjointness is this pick,
/// not beginSpill priority. Empty / missing is refuse, not a PostRA
/// share. PEI reserves one slot per LoopStart/SET; formed-ZOL and
/// refused demotes consume no slot. AIE has no stack save (reserved LC;
/// AIE2RegisterInfo.cpp:112-116, AIE2PFrameLowering.cpp:99-116 is RS-only).
/// Haydn SET reads a GPR, so the home is Haydn-owned.
int resolveDemoteSaveHome(int SaveFI, int PostRAScratchFI,
                          int BranchRelaxationScratchFI,
                          ArrayRef<int> StackCounterFIs);
int resolveDemoteSaveHome(int SaveFI, int PostRAScratchFI,
                          int BranchRelaxationScratchFI, int StackCounterFI);

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
///       admissible via the copy fallback PreheaderScr = LatchScr only
///       when !LatchScrLiveAtSet. Occupancy is occupiedAtSet (UseFromSet
///       || (LivePhysRegs-live && !DefInTail)), not any-mention
///       (regMentionedInPreheaderTail). D1.65: skipped copy plus Adj!=0
///       && !HasImm && !PreheaderScr refuses before the D1.51 emission
///       barrier rather than ST32 of full trip N. The 5-boolean cell-(d)
///       admit is unchanged (D1.19); extra refuse is the occupancy gate.
///       Overlay stale-value recurrence is D1.71(b). AIE SetLoopCount dest
///       is dedicated LC, src=trip, adj — never in-place Dest==Src on the
///       trip GPR (AIE2InstrInfo.cpp:1437 LCRegister=AIE2::LC;
///       AIEBaseHardwareLoops.cpp:408-414; getReachingLocalUses is LoopDec-
///       use only at :379). Hexagon COPY trip into a new vreg before LOOP_r
///       (HexagonHardwareLoops.cpp:1284-1291). RISC-V insertIndirectBranch
///       scavenges Define|Dead AllowSpill=false (RISCVInstrInfo.cpp:1433-
///       1471). Haydn SET reads a GPR, so the copy dest must be dead at SET.
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

/// Complete counted software-latch sequence law. Empty string = ok.
/// Applied only when \p MBB has countdown vocabulary (logical SUBI32, or
/// stack-counter LD32+SUBI32+ST32). Ordinary branches without that
/// vocabulary do not fire.
///
/// Long form if LUI(mbb)+ADDI(same mbb)+JALR is present: pin
/// LUI → ADDI → cond(BEQZ or BNEZ) → JALR (NOP/pad-member tolerant).
/// Else short form: after isSoftLatchBnezOpcode only exit B / BEQZ_W /
/// BEQZ-on-R0 / NOP.
/// Stack-counter LD/ST (logical LD32/ST32 and baked S_LW_WITH_IMM /
/// S_SW_WITH_IMM) must match THIS latch's assigned dedicated counter FI
/// (D1.102), not membership in the assigned pool or in PostRAScratchFI /
/// BranchRelaxationScratchFI / the demote-save pool, with word-aligned
/// simm6 Off.
/// \p RequireLatch also rejects a countdown MBB missing the counted edge.
std::string countedSoftwareLatchViolation(const MachineBasicBlock &MBB,
                                          bool RequireLatch = false);

/// D1.150 dual-seat sibling of countedSoftwareLatchViolation. Empty
/// string = ok. Silent when \p MBB has no bound demote-save FI. Else
/// require an ST32/S_SW whose FixedStack MMO equals this latch's bound
/// save FI, distinct from this latch's bound counter FI. Require the
/// matching LD32/S_LW only when isLiveMBB(Exit), matching the emitter
/// at HaydnHardwareLoops.cpp (PendingSaveRestore && isLiveMBB(Exit)).
/// A sibling latch's save FI does not bless this pair; two latches must
/// not share one bound save FI.
std::string demoteSaveHomePairViolation(const MachineBasicBlock &MBB);

/// Counted-latch suffix after demote. Walk backward from the last
/// terminator, skip NOP / LUI / ADDI32 / ADDI32_W and stack-counter LD/ST
/// (LD32 / ST32 and the product catalog aliases S_LW_WITH_IMM /
/// S_SW_WITH_IMM that late exact-commit bakes ST32/LD32 into). True iff
/// that window contains a SUBI32 countdown. Header==Latch body ops before
/// the window are not countdown glue (a body SUBI32 must not hide
/// erase-only once-through). When \p ForbidPreferCountdown, SUBI32 / LD32
/// / S_LW dest==Prefer is a named fatal — never LatchScr=Prefer on a
/// non-countdown body use. Occupancy miss last-resorts LatchScr=R0 rather
/// than Prefer.
/// AIE has no demote (AIE2InstrInfo.cpp:1437 LCRegister=AIE2::LC).
/// Hexagon FixupHwLoops.cpp:137-148 converts or leaves LOOP by range.
/// RISC-V insertIndirectBranch scavenges Define|Dead AllowSpill=false
/// (RISCVInstrInfo.cpp:1433-1471). Haydn SET reads a GPR; this stays a
/// free helper, not a new type. Fixup demoteOrFatalOccupancyMiss is the
/// one caller.
bool latchSuffixHasLegalCountdown(const MachineBasicBlock &Latch,
                                  Register Prefer,
                                  bool ForbidPreferCountdown);

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
///
/// D1.52: multi-survivor coissue routes through the one production commit
/// site commitOneProductCycle (probe + exact bake), never a probeless bake,
/// so the store/load may-alias law is enforced on the exact rematch path.
/// \p AA forwards the owning pass's AAResults so proven-NoAlias survivors
/// still coissue; null AA is fail-closed (the probe rejects an unproved
/// store/load pair and this falls back to per-member singletons).
void recommitSurvivingCycleMembers(ArrayRef<MachineInstr *> Keep,
                                   const HaydnInstrInfo &TII,
                                   const char *DebugPrefix,
                                   AAResults *AA = nullptr);

/// SET/LoopStart-member erase that preserves coissued siblings as an exact
/// product cycle. Dissolves the old root, erases only the setup member, then
/// recommits remaining children so consolidated root operands match the
/// surviving membership. \p AA is forwarded to recommitSurvivingCycleMembers
/// (same null-AA fail-closed law).
void eraseSetMemberAndRecommitSiblings(MachineInstr &SetMI,
                                       const HaydnInstrInfo &TII,
                                       const char *DebugPrefix,
                                       AAResults *AA = nullptr);

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

/// Collect the CFG loop region: Header, Latch, reverse-reachable
/// interior (Visited/insert bounds the walk — no 256-cap admit), and
/// early-exit-only blocks forward-reachable from Header that are not
/// the unique designated Exit (D1.101). Preheader and that Exit stay
/// out. When Latch has 0 or >1 live non-Header successors the forward
/// walk only takes immediate extras of non-Latch region blocks. D1.105
/// demote keeps those extra latch successors (drop SET, install
/// countdown) rather than refusing or dropping Extra.
void collectLoopBlocks(const MachineBasicBlock *Header,
                       const MachineBasicBlock *Latch,
                       const MachineBasicBlock *Preheader,
                       LoopBlockSet &Out);

/// Post-rewrite latch successors the scratch probes must honor.
/// Always Header, then the designated software Exit (so
/// recoverDemoteSetupFrom sees Exit before Extra). When \p KeepExtraSuccs,
/// also every current latch successor the D1.105 rewrite keeps. Dropped
/// extras (nsichneu Header==Latch early-exit) stay out: they are not
/// KeepExtra. AIE splitLoopEndJump (AIEBaseHardwareLoops.cpp:232-272)
/// makes a unique fallthrough so the ZOL latch never has Extra; Haydn
/// D1.105 keeps Extra instead of splitting (dedicated-exit is GR1.2/GR1.4).
/// Overlay: pass this set into the existing PostSuccs-restricted probes
/// (AIE SpillExpandHelper computeLiveOutsAt / LivePhysRegs::stepBackward).
void collectPostRewriteLatchSuccessors(
    MachineBasicBlock &Latch, MachineBasicBlock *Header,
    MachineBasicBlock *Exit, bool KeepExtraSuccs,
    SmallVectorImpl<MachineBasicBlock *> &Out);

/// D1.34 ONE estimate law (shared authority): conservative layout byte
/// growth charged when entering an aligned MBB — the joint parcel-grid /
/// BranchRelaxation-conservative law, unit-pinned:
///   (a) whole-parcel: Haydn emits whole product parcels, so the charged
///       start stays a parcel multiple whenever the running offset is;
///   (b) joint grid: the charged start is >= the first
///       lcm(MBB alignment, product parcel bytes) grid point — the same
///       grid HaydnMCELFStreamer::emitCodeAlignment and
///       HaydnMachineAlignment walk (Bytes=12, align 16 -> 48; the raw
///       alignTo-gap parcel rounding that returned 24 satisfies neither
///       the alignment nor the grid);
///   (c) BR-conservative: the charged start is >= generic
///       BranchRelaxation's postOffset model, alignTo(Bytes, A) plus the
///       (A - ParentAlign) uncertainty term when A exceeds the function
///       alignment (BranchRelaxation.cpp BasicBlockInfo::postOffset), so
///       no consumer can measure a span as near that the post-stamp BR
///       re-scan measures far (PO=96, A=32, PA=1: BR model 127 -> 132,
///       not the 96 the bare lcm grid allows).
/// Align(1) and negative-Bytes inputs return Bytes unchanged: the entire
/// alignment-1 corpus (every gr27/D1.33 boundary pin) charges zero pad.
int64_t padLayoutBytesForMBBAlign(int64_t Bytes,
                                  const MachineBasicBlock &MBB);

/// D1.34 ONE byte-walk authority: signed per-MBB start offsets in layout
/// order (BranchRelaxation scanFunction law) — cumulative
/// TII.getInstSizeInBytes plus the entering-MBB pad from
/// padLayoutBytesForMBBAlign. The entry block charges no pad (its
/// alignment is the function alignment, outside the branch-distance
/// window). \p Starts is assigned MF.getNumBlockIDs() entries; blocks
/// not seen live in layout order (dead / foreign numbers) keep the -1
/// sentinel. The pre-S1 normalizer consumes this scan directly (its
/// private static fork is deleted, not duplicated); the exported
/// distance/span estimators below are derivations of the same walk.
void computeLayoutBlockStarts(const MachineFunction &MF,
                              const TargetInstrInfo &TII,
                              SmallVectorImpl<int64_t> &Starts);

/// D1.34 ONE byte-walk authority: signed offset of \p It inside \p MBB
/// (BranchRelaxation getInstrOffset law: sizes of the preceding instrs;
/// \p It == end() yields the whole-block size). getInstSizeInBytes is
/// the single skip law — meta/debug/kill/implicit-def/CFI instrs charge
/// 0 there; no private hand skip-set exists beside it.
int64_t estimateLayoutInstrOffset(
    const MachineBasicBlock &MBB, MachineBasicBlock::const_iterator It,
    const TargetInstrInfo &TII);

/// D1.34 derivation of the one byte walk: signed layout distance in
/// bytes from \p FromIt (exclusive) in \p FromMBB to the START of \p
/// ToMBB, charging the entering-MBB pad for every block entered after
/// FromMBB. Returns -1 when either block is dead or ToMBB precedes
/// FromMBB in layout order. The Fixup Off1/Off2 windows (via
/// HaydnFixupHwLoops::estimateMBBDistance) consume exactly this walk.
int64_t estimateLayoutMBBDistance(const MachineFunction &MF,
                                  const MachineBasicBlock *FromMBB,
                                  MachineBasicBlock::const_iterator FromIt,
                                  const MachineBasicBlock *ToMBB,
                                  const TargetInstrInfo &TII);

/// SET-anchored start of the last size-bearing non-terminator in
/// Header/interiors/Latch (golden HWLR_END). PLE-only SMS latches still
/// resolve END from Header; a Latch-only walk would yield LastCycleStart<0.
/// \p HeaderStartOff is the SET-anchored byte offset of Header begin.
/// Returns -1 when Header/Latch are dead, Latch precedes Header, or no
/// size-bearing non-term exists in the region.
int64_t estimateLastBodyCycleOffset(const MachineFunction &MF,
                                    const MachineBasicBlock *Header,
                                    const MachineBasicBlock *Latch,
                                    const MachineBasicBlock *Preheader,
                                    int64_t HeaderStartOff,
                                    const TargetInstrInfo &TII);

/// D1.34 derivation of the one byte walk: inclusive layout span in bytes
/// from the START of \p FromMBB through the END of \p ToMBB. This is the
/// latch-backedge quantity the hwloop-demote LongLatch decision measures
/// (the short BNEZ_W at the latch end targets the header start).
///
/// D1.149: the walk is the pre-emission span. demoteHardwareLoopToSoftware
/// charges unrotated in-span parcels it will insert before the short
/// backedge (stack LD32+SUBI32+ST32, LatchEndSave ST32, Header-begin XOR)
/// onto BackedgeDisp before TII.isBranchOffsetInRange. The walk itself
/// stays the one D1.34 authority and does not compose D1.33's buffer or
/// D1.35 MaxHwLoopDemoteGrowthBytes / PreS1PostStampGrowthBytes. Free-arm
/// SUBI32, exit-B, and rotated ForwardBytes are not charged here.
///
/// Sentinel law: returns -1 when either block is dead or ToMBB does not
/// follow FromMBB in layout order (latch-before-header included). A -1
/// is a DIRECTION refusal, never a magnitude: the consumer must resolve
/// the site displacement on the SAME walk in the other direction
/// (estimateLayoutMBBDistance with the operands swapped) and fail closed
/// only when NEITHER direction is measurable; the demote never feeds
/// -(-1) = +1 into the range oracle as a short displacement.
int64_t estimateLayoutSpanBytes(const MachineFunction &MF,
                                const MachineBasicBlock *FromMBB,
                                const MachineBasicBlock *ToMBB,
                                const TargetInstrInfo &TII);

/// True if any MI in \p Blocks mentions \p Reg (use or def).
bool regMentionedInBlocks(Register Reg, const LoopBlockSet &Blocks);

/// True if \p Reg is defined in \p Blocks by a non-countdown op (load dest,
/// move, etc.). Pure residual countdown (Reg+=-1 / Reg-=1) is allowed.
/// Regmask-only clobbers (body calls) are invisible here — counter ownership
/// is decided by isSoundDemoteCounter. Defs-only: a body READ is not a
/// clobber (CB-165 save placement). The uses-side twin is
/// regUsedNonCountdownIn.
bool regClobberedNonCountdownIn(Register Reg, const LoopBlockSet &Blocks);

/// True if any non-countdown MI in \p Blocks reads \p Reg (`MO.readsReg()`,
/// including tied/implicit uses). Residual ±1 / LoopDec steps are skipped
/// so they are not body uses of the trip. Latch-block terminator zero-tests
/// of the same register (LoopJNZ, isSoftLatchBnezOpcode BNEZ/BNEZ_W, and
/// BEQZ/BEQZ_W) are also skipped: the L2 latch-terminator sweep erases them
/// before SUBI32+BNEZ_W, so they are not live-in-body reads. Pass \p Latch;
/// do not skip header/interior/early-exit zero-tests. Do not fold zero-tests
/// into isCountdownStepOf (stripResidualCountdown would erase live conds).
/// Defs-only clobbers stay with `regClobberedNonCountdownIn` (CB-165).
/// Both Prefer-as-counter and LatchScr=Prefer require this false.
bool regUsedNonCountdownIn(Register Reg, const LoopBlockSet &Blocks,
                           const MachineBasicBlock *Latch);

/// Counter-ownership law for a software-loop demote on \p Reg whose live
/// range is the loop blocks plus the preheader tail from \p PreheaderFrom.
/// The tail may contain ordinary setup-distance work after SET (SET is not
/// a scheduling boundary); any def/use of \p Reg there refuses the register.
/// A reaching frame-address last-def (va-arg-22 `$r3=sp+off`, including an
/// unmentioned pred live-through) also refuses: pickCounterReg would
/// otherwise copy the trip into the cursor.
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

/// Shared late-def CSR predicate (D1.87). Caller-saved is sound.
/// ABI callee-saved (R14) is sound iff this function's prologue saved
/// it (CalleeSavedInfo). Unsaved CSR write is never repaired: demote
/// runs post-PEI. isSoundDemoteCounter uses this for countdown GPRs;
/// pickDeadLatchScratch pass 1b uses it so unused R14 is not LatchScr
/// without CSI proof.
bool calleeSavedLateDefIsSound(MCPhysReg Reg, const MachineFunction &MF,
                               const TargetRegisterInfo &TRI);

/// True if any real MI in \p Preheader after \p PreheaderFrom overlaps
/// \p Reg. Skips the setup MI and the SET member (SET reads Prefer).
/// Other members of the setup bundle are the tail: SET is not a scheduling
/// boundary, so a GPR dead at the SET iterator can still be the address
/// scratch before the header (va-arg-22, including coissued r3=sp+off).
/// Walk is bundle-safe: rewind to the SET-bundle start and do not skip
/// From via a PastSetup pointer match (that drops a bundled after-SET
/// ADDI so pickCounterReg copies the trip into r3=sp+off). Occupancy
/// UseFromSet/DefInTail keep their own PastSetup walker. Do not fold the
/// CSR/call half of isSoundDemoteCounter into this — LatchScr is not a
/// free countdown.
bool regMentionedInPreheaderTail(
    MCPhysReg Reg, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom,
    const TargetRegisterInfo &TRI);

/// True if a real tail MI after SET/setup reads \p Reg with a reaching
/// def at or before \p PreheaderFrom. Occupancy keeps its PastSetup
/// SET/setup-bundle skip (do not fold this into the any-mention walker:
/// isSoundDemoteCounter and Prefer-as-LatchScr still need defs).
/// Same-MI: collect reads vs defs first; if reads && !definedInTail the
/// SET-site value is live into that use (cell-(d) copy must not clobber
/// it), then apply the def. A pure tail def is not a SET-site use.
/// AIE has no scavenged-GPR overlay (AIE2InstrInfo.cpp:1437 LCRegister=
/// AIE2::LC; AIEBaseHardwareLoops.cpp:379 getReachingLocalUses is LoopDec-
/// use only). Haydn SET reads a GPR; these stay free helpers, not a new
/// occupancy type.
bool regUsedFromSetInPreheaderTail(
    MCPhysReg Reg, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom,
    const TargetRegisterInfo &TRI);

/// True if any real tail MI after SET/setup defines \p Reg. Same skip as
/// regMentionedInPreheaderTail. A def-in-tail kills SET-site occupancy:
/// PreheaderSave would store a stale or undefined pre-tail value.
bool regDefdInPreheaderTail(
    MCPhysReg Reg, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom,
    const TargetRegisterInfo &TRI);

/// True if \p Reg has an incoming value: an in-function MRI def, or a
/// live-in of MF.front() (function entry). Entry live-ins count as a
/// value without a def in-function. Never FunctionPhysLiveness CSI /
/// isLiveAfterLoop — that seed kept saved CSRs live in every block.
/// Stored MBB live-ins of Header/Latch/Exit are not this predicate.
/// Pass 1a requires this true; pass 1b is the unused dead temp. The
/// caller installs no overlay pair; skip-overlay is skip-only debug.
/// Unused R14 with no def and no entry live-in is pass-1b only when
/// calleeSavedLateDefIsSound (CSI).
/// Occupancy walkers stay; this is not a new occupancy type. AIE has
/// no scavenged-GPR overlay (AIE2InstrInfo.cpp:1437 LCRegister=
/// AIE2::LC). pickDeadLatchScratch keeps using this.
bool hasIncomingValue(MCPhysReg Reg, const MachineFunction &MF);

/// True if the last def of \p Reg that reaches header entry is a
/// frame-address materialize from SP (R13) or, when hasFP, FP (R14):
/// ADDI32 / ADDI32_W / ADD32 / SUBI32 / SUB32 / MOVE32 / COPY. ADD32 is
/// commutable so either source may be SP/FP; SUB/SUBI use the minuend.
/// COPY/MOVE of another FA last-def is the same value. ADDI/ADD/SUBI/SUB
/// of an FA last-def is the same family: va-arg-22 `$r4 = r3+imm` addr
/// temps that are not themselves SP last-defs. `$r3 = sp+off` is this
/// shape (SET-bundle coissue, a reaching def before SET, post-SET tail,
/// or an unmentioned pred live-through). A DEF from r0/imm (SMS guard)
/// or a load from the frame is not. A frame-address killed before header
/// entry is not (pass-1a dead temp). Do not fold this into the any-mention
/// or UseFromSet/DefInTail walkers. LatchScr NoSpill re-pick uses this so
/// general tail mentions do not exhaust the scratch (bqriir).
/// pickDeadLatchScratch honors Exclude via regsOverlap and does not take
/// Preheader/InsPt.
bool regIsPreheaderTailFrameAddress(
    MCPhysReg Reg, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom,
    const TargetRegisterInfo &TRI);

/// True if \p Reg is in the after-SET ADDI r0,imm copy-chain whose value
/// is live into \p Exit. Last def ADDI32 / ADDI32_W from R0 with an
/// immediate, or COPY/MOVE of such a last-def, is the chain; if any
/// member has an actual Exit-path read, every member is this class —
/// including the ADDI dest a COPY/MOVE killed and a dest the header
/// redefines (`$r14=ADDI r0,imm; $r4=COPY $r14; header $r14=COPY $r4`).
/// va-arg-22: after the FA skip of `$r3=sp+off`, skipCommon must not
/// keep that `$r14`; latch LD32/SUBI/BNEZ would clobber the exit value.
/// Empty LatchScr after the skip is occupancy miss, not a steal.
/// Walk is bundle-safe from the SET bundle root (coissued non-SET
/// members and later unbundled instrs); a PastSetup pointer match on
/// instrs() misses a bundled SET. An SMS-guard ADDI r0,imm with no
/// Exit-path read of the family is not this class and stays a sound
/// pass-1a/1b latch temp. A DEF from SP/FP is FA, not this. Do not walk
/// preds or pre-SET defs. Do not use LivePhysRegs/addLiveOuts here: R14
/// is a CSR and pristines would false-positive unused R14. Do not fold
/// into isCountdownStepOf, occupiedAtSet, or regMentionedInPreheaderTail.
/// pickDeadLatchScratch honors Exclude via regsOverlap and skips the
/// named LatchExcl predicate (unmentioned Exit-read FA ∪ this copy-chain)
/// on every pass, including empty Exclude, so 1a/1b cannot keep
/// `$r3=sp+off` or `$r14=ADDI r0,imm` after NoSpill exhausts. Empty
/// LatchScr after a correct skip is occupancy miss, not a steal.
/// \p SkipBlocks, when set, is the LoopBlocks skip for
/// the Exit-path walk (same as FA); header redef of the ADDI dest is a
/// loop-block mention and must not hide the chain. AIE SetLoopCount dest
/// is dedicated LC (AIE2InstrInfo.cpp:1437,
/// AIEBaseHardwareLoops.cpp:411-414); Hexagon COPY trip into a new vreg
/// (HexagonHardwareLoops.cpp:1284-1291); RISC-V insertIndirectBranch
/// scavenges Define|Dead AllowSpill=false (RISCVInstrInfo.cpp:1433-1471).
/// Haydn SET reads a GPR — this stays a free helper, not a new type.
bool regIsLiveIntoExitTailImm(
    MCPhysReg Reg, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom,
    const MachineBasicBlock *Exit, const TargetRegisterInfo &TRI,
    const LoopBlockSet *SkipBlocks = nullptr);

/// Single LatchExcl predicate: \p Reg is unsound as LatchScr when it is
/// in the after-SET ADDI r0,imm / COPY/MOVE copy-chain live into \p Exit
/// (header redef of the ADDI dest is a loop-block mention and must not
/// hide this), or an unmentioned preheader-tail frame-address last-def
/// that Exit actually reads (va-arg-22 `$r3=sp+off`, the main() fill-loop
/// cluster of SP+off address temps live into Exit, and r3+imm dests that
/// are not themselves SP last-defs). Unmentioned live-through that is
/// not FA is skipLatchScrReason / skipCommon Exit-path, not this
/// predicate. Mentioned FA GPRs and PEI SP+off dests that Exit does not
/// read stay eligible pass-1a dead temps (bqriir). Same-MI def+use is
/// not an incoming Exit read. Occupancy is not this predicate; do not
/// restamp occupiedAtSet.
bool regIsUnsoundLatchScratchAtSet(
    MCPhysReg Reg, const MachineBasicBlock *Preheader,
    MachineBasicBlock::const_iterator PreheaderFrom,
    const MachineBasicBlock *Exit, const LoopBlockSet &Blocks,
    const TargetRegisterInfo &TRI);

/// True iff a CFG walk from \p Exit cannot prove \p Reg unread on the
/// Exit path (loop blocks skipped). LatchExcl feeds this into NoSpill
/// and pickDeadLatchScratch skipCommon so a trampoline Exit with empty
/// stored live-ins cannot hide a successor ST32 of a GPR that is dead at
/// latch end vs {Header, Exit}. Same-MI def+use is not an incoming read.
/// BUNDLE-header uses (finalizeBundle ExternUses) are incoming even when
/// the same header also has aggregated Defs (WAR/snapshot). Header Defs
/// are not KilledIncoming before members. InternalRead is not a read.
/// PEI dests and SMS-guard ADDI with no Exit-path use stay eligible.
bool regExitPathReadsPhysReg(MCPhysReg Reg, const MachineBasicBlock *Exit,
                             const MachineBasicBlock *Preheader,
                             const LoopBlockSet &Blocks,
                             const TargetRegisterInfo &TRI);

/// True if \p MI is a residual countdown step of \p Reg: generic
/// HardwareLoops LoopDec, or a leftover Prefer+=-1 / Prefer-=1 whose
/// step is a proven ±1 immediate. Same-reg ADD32/SUB32 with a register
/// rhs (any Reg ± R2) is an unverifiable addend and must return false
/// so stripResidualCountdown cannot erase live compute. Do not fold
/// latch/header zero-tests into this (strip would erase live conds).
bool isCountdownStepOf(const MachineInstr &MI, Register Reg);

/// True when leftover ±1 steps of \p Reg in \p Blocks are equivalent to
/// a single SUBI32 reinstalled at \p Latch end. Residuals of \p Reg
/// outside Latch plus regUsedNonCountdownIn is a one-iteration value
/// shift (false). Latch-only residuals, or header-side residuals whose
/// only consumers are L2-erased latch zero-tests, are equivalent (true).
/// Vacuous (no residual / non-phys) is true. LoopDec is not a leftover
/// strip target. Do not fold zero-tests into isCountdownStepOf.
/// AIE LoopDec may only feed LoopEnd (AIEBaseHardwareLoops.cpp:376-383);
/// SetLoopCount writes dedicated LC (aie2/AIE2InstrInfo.cpp:1431-1437)
/// — AIE has no software-demote leftover strip. Hexagon FixupHwLoops
/// converts or leaves LOOP (HexagonFixupHwLoops.cpp:137-148). Haydn SET
/// reads a GPR; this proves the leftover law instead of asserting it.
bool residualCountdownEquivalentAtLatch(const LoopBlockSet &Blocks,
                                        Register Reg,
                                        const MachineBasicBlock *Latch);

/// Erase residual countdown steps of \p Reg from every block in \p Blocks
/// so demote's SUBI32+BNEZ_W is the sole decrement. Walk instrs for
/// BUNDLE interiors. Caller must prove residualCountdownEquivalentAtLatch
/// (or refuse) before mutation; header-side leftover is not automatically
/// latch leftover. Strip only the register that receives the latch SUBI32.
/// Bundled victims are grouped by original BUNDLE root, dissolved once,
/// and survivors go through recommitSurvivingCycleMembers (exact
/// multi-member commit or singleton fallback). Bare MIs keep
/// eraseInstrSafe. \p TII is the late setDesc/singleton path; AA is
/// null (fail-closed, same as sequentializeSetCycleIfMixed).
void stripResidualCountdown(const LoopBlockSet &Blocks, Register Reg,
                            const HaydnInstrInfo &TII);

/// Fill \p Live with the one-block live-ins of \p MBB: addLiveOuts then
/// reverse stepBackward. Stored MBB live-in lists are stale this late
/// (BranchRelaxation split tails may be empty). Overlay of AIE
/// AIELiveRegs.cpp:37-52 without the function-wide worklist class.
void computeBlockLiveIns(LivePhysRegs &Live, const MachineBasicBlock &MBB);

/// Seed \p Live with FunctionPhysLiveness live-ins of \p MBB's successors
/// (SeedPristines=false). Stored MBB live-ins are derived cache, never SET
/// occupancy authority (D1.71r): a stale extra name over-occupies SET, and a
/// stale-empty list hides a live-through. FPL already occupies a Header
/// live-in with no Header use, including Header→E second-hop (D1.138) and
/// Header→Latch live-through. AIE LiveRegs.cpp:37-107 computes successor
/// LiveIns and never reads MBB::liveins()/liveouts(); llvm::computeLiveIns
/// seeds from stored lists (LivePhysRegs.cpp:257-266). Never addLiveOuts
/// pristines (unused CSRs / FPL CSI would refuse a 1b temp). Occupancy at
/// SET and pickCounterReg SET seed consult this. LatchScr dead-at-end is
/// pickDeadLatchScratch's PostSuccs FPL seed, not this.
void addComputedSuccessorLiveIns(LivePhysRegs &Live,
                                 const MachineBasicBlock &MBB);

/// One-block live-ins of \p MBB computed as if its successors were exactly
/// \p Succs (the post-rewrite obligation set). A self-loop entry in \p Succs
/// is the PostSuccs-restricted least fixed point of \p MBB (never stored
/// liveins). Every other successor contributes its computed live-ins.
/// Extra pre-rewrite successors that are not in \p Succs do not occupy
/// the set — Header==Latch early-exit edges that the demote latch rewrite
/// drops must not refuse a legal LongScr.
void computeBlockLiveInsFromSuccessors(
    LivePhysRegs &Live, const MachineBasicBlock &MBB,
    ArrayRef<const MachineBasicBlock *> Succs);

/// True iff the computed live-ins of \p MBB contain \p Reg.
bool blockLiveInContains(const MachineBasicBlock &MBB, MCPhysReg Reg);

/// True iff \p Reg is a computed live-in of \p MBB under successor set \p Succs.
bool blockLiveInContainsFromSuccessors(
    const MachineBasicBlock &MBB, ArrayRef<const MachineBasicBlock *> Succs,
    MCPhysReg Reg);

/// D1.61: one alias-aware, edge-aware whole-function physical-liveness
/// fixed point, the single owner late scratch picking consults. The
/// per-block transfer is the guarded-tail step (first terminator steps
/// normally; every terminator strictly after it keeps uses and drops
/// defs/regmask kills — a def that may be skipped by an earlier
/// conditional kills nothing), the join is over CFG successors, and the
/// seed is pristines (valid-CSI unsaved CSRs, live for the caller)
/// plus saved-and-restored CSRs, applied to every block's live-out
/// basis (the conservative function-wide form: it can only keep a
/// register LIVE — refusal — never prove a live one dead). Tail
/// operands and regmasks enter through the transfer. Stored MBB live-in
/// lists are never read: the pipeline contract allows them only as a
/// cache of this owner (AIE SpillExpandHelper addLiveIns is that
/// intra-block cache; it is not whole-function authority). Self-edges
/// join the converging LiveIn of the block itself — the least fixed
/// point discovers loop-carried uses through the transfer. Every
/// consumer query is answered from the converged sets.
class FunctionPhysLiveness {
public:
  /// Compute the fixed point of \p MF. Idempotent on the unchanged
  /// function; one build per decision point (callers re-build after any
  /// mutation, e.g. LongBranchNormalize's fixed-point re-scan loop).
  /// \p SeedPristines is the D1.61 CSI / pristine seed (default): unsaved
  /// CSRs plus saved-and-restored CSRs on every block's live-out basis.
  /// Pass-1a / NoSpill / occupiedAtSet build with SeedPristines=false
  /// (AIE LiveRegs.cpp:37-107 worklist overlay: successor LiveIns are
  /// computed, never MBB::liveins()/liveouts(); no CSI seed). Default CSI
  /// FPL stays pickCounterReg / LongScr / LBN (D1.61r: CSI as 1a/1b seed
  /// occupancy-misses unused saved CSRs).
  void build(const MachineFunction &MF, bool SeedPristines = true);

  /// True iff \p Reg is live-in of \p MBB in the converged sets (alias
  /// closure through LivePhysRegs). Unknown MBB (not of the built MF)
  /// returns true — fail-closed.
  bool isLiveIn(const MachineBasicBlock &MBB, MCPhysReg Reg) const;

  /// Append the converged live-ins of \p MBB to \p Live. Unknown MBB is a
  /// no-op; isLiveIn remains the fail-closed occupancy query.
  void addLiveInsTo(LivePhysRegs &Live, const MachineBasicBlock &MBB) const;

  /// Computed live-ins of \p MBB for pass-1a / NoSpill / occupiedAtSet:
  /// reverse stepBackward of \p MBB seeded from converged live-ins of its
  /// non-backedge successors (AIE LiveRegs.cpp:37-107: those LiveIns are
  /// computed, never stored). Backedges (self and successors that are
  /// also predecessors) are omitted — FPL.isLiveIn(\p MBB) would join the
  /// latch backedge (loop-carried Latch XOR uses) and extras the rewrite
  /// drops (D1.136). \p Avoid is the origin latch when seeding a distinct
  /// PostSucc: successor live-ins of that hop are computed as if Avoid
  /// contributes nothing, so Header→EarlyExit→Latch does not occupy a
  /// Latch XOR dest, while Header→E / Header→Mid→E still see E's uses
  /// (D1.138). Never CSI (caller builds with SeedPristines=false).
  void addForwardLiveInsTo(LivePhysRegs &Live, const MachineBasicBlock &MBB,
                           const MachineBasicBlock *Avoid = nullptr) const;

  /// True iff \p Reg is live on entry of ANY CFG successor of \p MBB —
  /// the clobber-obligation query for a rewrite that keeps all edges
  /// (a write that always executes destroys the value on the edge that
  /// carries it).
  bool liveOnAllSuccessors(const MachineBasicBlock &MBB, MCPhysReg Reg) const;

  /// True iff \p Reg is live on entry of \p DeadOn only. The narrowed
  /// clobber obligation (ZOL arm: parcels after PseudoLoopEnd run only
  /// on the software-exit fallthrough).
  bool liveOnSuccessor(const MachineBasicBlock &MBB,
                       const MachineBasicBlock *DeadOn, MCPhysReg Reg) const;

  /// True iff \p Reg is live under the POST-REWRITE successor obligation
  /// of \p MBB: exactly \p PostSuccs. A self entry contributes the
  /// restricted-successor least fixed point of \p MBB (AIE
  /// SpillExpandHelper computeLiveOutsAt transfer — addLiveOuts +
  /// stepBackward — iterated under PostSuccs only), never stored MBB
  /// liveins and never the full-CFG converged LiveIn of \p MBB (that
  /// joins pre-rewrite successors the rewrite DROPS). Every other entry
  /// contributes one guarded body walk seeded from the CONVERGED
  /// live-ins of that successor's own successors. Extra pre-rewrite
  /// successors that the rewrite drops are NOT obligations (the
  /// Header==Latch early-exit class: they must not refuse a legal
  /// scratch).
  bool liveUnderPostRewriteSuccessors(const MachineBasicBlock &MBB,
                                      ArrayRef<const MachineBasicBlock *>
                                          PostSuccs,
                                      MCPhysReg Reg) const;

private:
  const MachineFunction *MFPtr = nullptr;
  /// Converged live-in sets, indexed by MBB getNumber().
  std::unique_ptr<LivePhysRegs[]> LiveIn;
  /// The pristine/saved-CSR seed set (see build).
  LivePhysRegs SeedRegs;
  size_t NumBlocks = 0;
};

/// Pick a free GPR for soft-loop countdown at \p InsertPt in \p Preheader.
/// Returns invalid Register if none is free (caller must refuse erase-only
/// on a live body — fatal via recoverRangeOrOrder).
Register pickCounterReg(const LoopBlockSet &Blocks, Register Prefer,
                        const HaydnSubtarget &ST,
                        MachineBasicBlock &Preheader,
                        MachineBasicBlock::iterator InsertPt,
                        const char *DebugPrefix);

/// Last-resort stack-counter latch scratch at \p Latch end versus the
/// post-rewrite successors \p PostSuccs (Header, Exit, kept Extras).
/// Dead-at-end seed is not findPostRAScratchNoSpill: distinct successors
/// contribute FunctionPhysLiveness live-ins built with SeedPristines=false
/// (AIE LiveRegs worklist — computed, never stored; llvm::computeLiveIns
/// bottoms out in successor stored liveins and misses a Header→E
/// second-hop), and a self successor uses the PostSuccs-restricted least
/// fixed point of \p Latch, never stored liveins (those join the preheader
/// edge and hide a killed preheader temp) and never FPL.isLiveIn(Latch) /
/// default CSI FPL (D1.61r: CSI occupies saved CSRs in every block and
/// would refuse a dead 1b temp; full-CFG LiveIn[Latch] joins extras the
/// rewrite drops). Candidate universe
/// is allocatable GPR32NoSPNoLR rather than NoSpill's truncated
/// priority list. A body temp dead in the latch window is a sound
/// stack-counter scratch even when mentioned in LoopBlocks. Skip R0
/// and reserved regs. \p Exclude is the D1.64/CB-162 Prefer key —
/// never return a register the caller already refused as LatchScr.
///
/// Pass 1a is dead-at-latch-end with an incoming value. Pass 1b is a dead
/// unused temp whose late def is CSI-sound (D1.87): unused R14 is LatchScr
/// iff the prologue saved it. LD32 defines it. After 1a/1b miss, return
/// empty. Do not steal an unmentioned live-through: the caller installs
/// no preservation pair (skip-overlay is skip-only debug). Empty LatchScr
/// is occupancy miss; the caller last-resorts LatchScr=R0 (soft-zero) with
/// Header-begin XOR restore rather than Fixup LLVM ERROR. Prefer stays in
/// Exclude when it has non-countdown body uses. Honor Exclude via
/// regsOverlap.
/// skipCommon refuses R0/reserved/Exclude and LatchExcl: live-into-exit
/// tail-imm copy-chain ∪ every incoming GPR cfgPathReadsPhysReg cannot
/// prove absent on the Exit path (unmentioned FA, trampoline-Exit
/// successor reads, WAR/snapshot incoming). Stored live-ins / one-block
/// LivePhysRegs are not that proof, so 1a cannot keep a register that is
/// LPR.available only because a trampoline-Exit successor read was
/// invisible. Genuine dead temps with no Exit read stay 1a/1b (bqriir
/// PEI dests, SMS-guard ADDI). Including empty Exclude, 1a/1b cannot keep
/// `$r3=sp+off` or `$r14=ADDI r0,imm` after NoSpill exhausts. Unused
/// unsaved R14 is occupancy miss, not pass-1b. Empty LatchScr after a
/// correct skip is occupancy miss, not a steal. Do not take
/// Preheader/InsPt. Do not copy LatchScr onto the Adj PreheaderScr when
/// it is occupied at SET. Do not pick preheader-tail $r3 that Exit reads
/// as LatchScr. Last-resort LatchScr=Prefer remains CB-162 when
/// skipLatchScrReason is clear; Adj!=0 takes it only with a PreheaderScr
/// (occupancy miss otherwise — never ST32 of the full trip).
/// AIE SET dest is dedicated LC (AIE2InstrInfo.cpp:1338-1346
/// LCRegister=AIE2::LC; AIEBaseHardwareLoops.cpp:411-414 ADD_NC into LC)
/// and AIE has no GPR latch-scratch overlay. Hexagon copies the trip into
/// a new vreg before LOOP_r (HexagonHardwareLoops.cpp:1284-1291). RISC-V
/// insertIndirectBranch scavenges Define|Dead AllowSpill=false and
/// restores only in an empty RestoreBB with pred_size()==1
/// (RISCVInstrInfo.cpp:1433-1471).
Register pickDeadLatchScratch(MachineBasicBlock &Latch,
                              ArrayRef<MachineBasicBlock *> PostSuccs,
                              const LoopBlockSet &Blocks,
                              ArrayRef<Register> Exclude,
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
