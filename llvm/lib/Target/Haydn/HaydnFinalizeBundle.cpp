//===- HaydnFinalizeBundle.cpp ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Port of AIEFinalizeBundle (AIEFinalizeBundle.cpp:22-54 isBundleCandidate +
// runOnMachineFunction loop). Haydn delta vs AIE:
//
//   * After finalizeBundle, stamp Format E BundleFormatRowID +
//     CompletionStateID as the BUNDLE-root imms (sole product identity).
//   * late layout firewall: before wrap, materialize bare multi-slot
//     logicals via empty-cycle tryAdd → setDesc(member)
//     (AIEMachineScheduler.cpp:1121-1139 materializeMultiOpcodeInstrs;
//     AIEHazardRecognizer.cpp:174-214 alt try; HaydnBundleMaterialize
//     commitLateProductCycle). Idempotent on already-bundled / already-
//     setDesc members. Ensures encode sees real placement members — no
//     residual logical pack after FE8.
//   * FieldSlot→MemberId: after row+completion stamp, rewrite residual
//     `_S*` children to generated Format E members keyed by (logical, mode,
//     entry). Leading membership order is tried first; suffix digits never
//     pin entries. Packs that cannot bind in membership order (store only
//     at E3 e0 LOADSTORE0; dual loads LS0+LOAD1) use the same bounded
//     assignFormatEMemberEntries DFS as standalone hand-asm — placement
//     owns entry assignment, MC does not. Encoder then takes the typed
//     MemberId path (scatter by committed EntryIdx). Fail closed: no keep
//     map from FieldSlot operands onto the member Desc, member TIED_TO
//     missing on FieldSlot, or RI6 immediates outside simm6. Extra trailing
//     uses (MOVE32/ABS32 vestigial rs2) and tied extra acc ins (MAC
//     `$rta = $rtd`) drop on rewrite — generated members own operand shape.
//     Extra FieldSlot ties (ALU64 `$rd = $rsd`) drop on setDesc.
//     Catalog role `reg` is an SSA out for LUI/ZERO_GPR/ZERO_DR/CSRR/
//     MOVESFR2GPR; vestigial LUI $rs (first ins) drops. Compact reloc
//     (BEQZ_S0) and reloc-bearing branch/call `_W`
//     (JAL_W_S0 / JALR_W_S0) cutover: members use the WIDE PCRel operand
//     class; JALR link is an SSA out. Reloc ADDI32_W/ORI32_W cutover with
//     simm20_wide_abs/uimm20_wide_abs. Reloc SET_HWLOOP*_W cutover
//     (uimm6/uimm12 + getExprFixupKind HWLoopOff). POST/PRE/BREV members
//     carry a tied dest2 writeback matching FieldSlot `$rs = $rs_wb`.
//     Unsuffixed catalog logicals and alias/`_W` forms (`MULA64_HH`,
//     `CSRW_W`, `ST32_POST`) try the same rewrite when Desc+TIED_TO match;
//     unlike `_S*` they do not fail the whole bundle if incompatible.
//     Residual SET_HWLOOP (peel-identity) stays for the verifier ban. Reloc
//     CSRW_W cutovers to catalog CSRW I8 members; encode binds typed CSR I8
//     (FIXUP_HAYDN_CSR_UImm8 / R_HAYDN_CSR_UImm8), never untyped NONE. Pad
//     NOP/NOP_S0 is
//     CompletionState, not membership — skip without consuming an entry and
//     erase co-issued pads after a successful rewrite. Pad-NOP census law
//     (W28/CR-B3, encoding F11): every completion/count decision below is
//     derived from the SAME shared census the verifier replays
//     (haydn::bundle::collectBundleMemberOpcodes / bundleHasPadNop /
//     selectCompletionForMembersAndPads) — no second counting walk, so a
//     hand `BUNDLE { NOP }` or previously finalized root always verifies
//     under the rule this stamper applied. Product print is the
//     golden mnemonic (`jal`/`jalr`/`addi32`/`set_hwloop_f2`/`s_lw_post_imm`/
//     `csrw`), matching objdump.
//
// Pipeline:
//   * addPreSched2 after PostMachineScheduler (AIE2TargetMachine.cpp:242-244)
//   * addPreEmit after BR→FixupHwLoops→BR growth (Haydn-only; AIE PreEmit
//     empty AIE2TargetMachine.cpp:88 / AIEBaseTargetMachine.cpp:388)
//
//===----------------------------------------------------------------------===//

#include "HaydnFinalizeBundle.h"
#include "Haydn.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundleVerify.h"
#include "HaydnBundlePlan.h"
#include "HaydnFormatERecords.h"
#include "HaydnInstrInfo.h"
#include "HaydnMemberSetDesc.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/Debug.h"
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <mutex>

using namespace llvm;

#define GET_FORMAT_E_MEMBER_OPCODES
#include "HaydnGenFormatEMemberOpcodes.inc"

#define DEBUG_TYPE "haydn-finalize-mi-bundles"

namespace {

// Port of AIEFinalizeBundle.cpp:22-36 isBundleCandidate.
// AIE also skips isHardwareLoopEnd; Haydn PseudoLoopEnd is already
// isMetaInstruction (skipped below). LoopDec/LoopJNZ are real and wrap.
bool isBundleCandidate(MachineBasicBlock::instr_iterator MII) {
  MachineInstr *MI = &*MII;
  // Meta / debug / CFI / lifetime / KILL / ImplicitDef — not encode cycles.
  // Already-bundled MIs (BUNDLE roots, or children). A BUNDLE header without
  // BundledSucc (hand MIR) is still a committed root — do not wrap it as a
  // late singleton (that would stamp a missing-row E2 default).
  if (MI->isMetaInstruction() || MI->isBundle() || MII->isBundled())
    return false;
  // Inline asm is a TargetOpcode pseudo lowered by AsmPrinter::emitInlineAsm
  // on the top-level MI. Wrapping it as a BUNDLE child drops the APP block
  // (BUNDLE path skips non-SETCBR pseudos). Leave standalone.
  if (MI->isInlineAsm())
    return false;
  return true;
}

/// Keep-list from FieldSlot explicit operands onto generated member Desc.
/// Closed drop rules (no bag-sort by register class):
///  * identity when counts match
///  * drop every ins TIED_TO a def (MAC/MOVT seed-copy acc; dual-dest MAC
///    drops two acc ins)
///  * trailing extra uses when NumDefs matches and no ins is tied to a def
///    (MOVE32/ABS32 vestigial rs2)
///  * skip first ins when NumDefs match and trailing drop fails
///    (LUI vestigial $rs between dest and imm)
/// Extra implicit-defs (SETCBR expand CBR) are ImplicitTail, not keep-map.
/// LUI/ZERO_GPR cutover requires generated dest as SSA out (catalog role
/// `reg`). Skip-Finalize / hand-asm FieldSlot uses the same keep-map;
/// class-bag rebuild is not a fill path.
std::optional<SmallVector<unsigned, 4>>
fieldSlotKeepOperands(const MachineInstr &MI, const MCInstrDesc &NewDesc) {
  const MCInstrDesc &OldDesc = MI.getDesc();
  const unsigned OldN = OldDesc.getNumOperands();
  const unsigned NewN = NewDesc.getNumOperands();
  // Operand-less on both sides (ZERO_SFR and its members) is trivially
  // compatible: the empty keep map. Only asymmetric zero is a mismatch.
  if (OldN == 0 && NewN == 0)
    return SmallVector<unsigned, 4>{};
  if (OldN == 0 || NewN == 0 || MI.getNumExplicitOperands() < OldN)
    return std::nullopt;

  auto kindOk = [&](unsigned OldI, unsigned NewI) -> bool {
    const MachineOperand &MO = MI.getOperand(OldI);
    const MCOperandInfo &Info = NewDesc.operands()[NewI];
    const bool WantReg =
        Info.OperandType == MCOI::OPERAND_REGISTER || Info.RegClass >= 0;
    if (WantReg)
      return MO.isReg();
    return MO.isImm() || MO.isMBB() || MO.isGlobal() || MO.isSymbol() ||
           MO.isCPI() || MO.isJTI() || MO.isBlockAddress() || MO.isMCSymbol() ||
           MO.isTargetIndex();
  };

  auto tiesOk = [&](ArrayRef<unsigned> Keep) -> bool {
    if (Keep.size() != NewN)
      return false;
    for (unsigned NewI = 0; NewI != NewN; ++NewI) {
      const int NewTie = NewDesc.getOperandConstraint(NewI, MCOI::TIED_TO);
      if (NewTie == -1)
        continue;
      if (static_cast<unsigned>(NewTie) >= NewN)
        return false;
      const int OldTie =
          OldDesc.getOperandConstraint(Keep[NewI], MCOI::TIED_TO);
      if (OldTie != static_cast<int>(Keep[NewTie]))
        return false;
    }
    return true;
  };

  auto Keep = haydnFormatEKeepOperands(OldDesc, NewDesc, kindOk);
  if (!Keep)
    return std::nullopt;
  if (!tiesOk(*Keep))
    return std::nullopt;
  return Keep;
}

} // namespace

/// FieldSlot→MemberId is legal when fieldSlotKeepOperands finds a keep
/// map. Extra FieldSlot ties (ALU64 `$rd = $rsd`) may drop; POST/PRE/BREV
/// member `$dest2_1 = $dest2_wb` must already exist on the keep map.
bool llvm::memberDescCompatible(const MachineInstr &MI, unsigned MemberOpc,
                                const TargetInstrInfo &TII) {
  const MCInstrDesc &NewDesc = TII.get(MemberOpc);
  return fieldSlotKeepOperands(MI, NewDesc).has_value();
}

/// setDesc to \p MemberOpc and rewrite explicit operands to the member
/// Desc order from the keep map. Drop-only is not enough: CSRW catalog
/// (uimm8, rs) vs a member that lists (rs, uimm8) must swap, not leave
/// an imm in a register slot.
void llvm::rewriteFieldSlotToMember(MachineInstr &MI, unsigned MemberOpc,
                                    const TargetInstrInfo &TII) {
  const MCInstrDesc &OldDesc = MI.getDesc();
  const MCInstrDesc &NewDesc = TII.get(MemberOpc);
  auto Keep = fieldSlotKeepOperands(MI, NewDesc);
  assert(Keep && "rewriteFieldSlotToMember requires memberDescCompatible");
  MachineFunction *MF = MI.getMF();
  assert(MF && "rewriteFieldSlotToMember requires a parent function");

  const unsigned OldN = OldDesc.getNumOperands();
  const unsigned NewN = NewDesc.getNumOperands();
  assert(Keep->size() == NewN && "keep map must cover every member operand");

  SmallVector<MachineOperand, 4> Kept;
  Kept.reserve(NewN);
  for (unsigned NewI = 0; NewI != NewN; ++NewI)
    Kept.push_back(MI.getOperand((*Keep)[NewI]));

  SmallVector<MachineOperand, 4> ImplicitTail;
  for (unsigned I = MI.getNumOperands(); I > OldN; --I)
    ImplicitTail.push_back(MI.getOperand(I - 1));

  SmallVector<unsigned, 4> DropTies;
  for (unsigned NewI = 0; NewI != NewN; ++NewI) {
    const unsigned OldI = (*Keep)[NewI];
    if (OldDesc.getOperandConstraint(OldI, MCOI::TIED_TO) != -1 &&
        NewDesc.getOperandConstraint(NewI, MCOI::TIED_TO) == -1)
      DropTies.push_back(NewI);
  }

  while (MI.getNumOperands())
    MI.removeOperand(MI.getNumOperands() - 1);
  MI.setDesc(NewDesc);
  for (const MachineOperand &MO : Kept)
    MI.addOperand(*MF, MO);
  for (unsigned I = ImplicitTail.size(); I > 0; --I)
    MI.addOperand(*MF, ImplicitTail[I - 1]);
  for (unsigned OpI : DropTies)
    MI.untieRegOperand(OpI);
}

namespace {

bool isWideResidualName(StringRef Name);
bool isWideCutoverLogical(StringRef Log);

/// setDesc bare multi-slot logical to empty-cycle tryAdd member before
/// finalizeBundle. Port of AIE materializeMultiOpcodeInstrs setDesc
/// (AIEMachineScheduler.cpp:1126-1132) on a late singleton cycle
/// (HaydnBundleMaterialize.h commitLateProductCycle).
bool materializeLateBareIfNeeded(MachineInstr &MI, const TargetInstrInfo &TII,
                                 HaydnMCFormats &Fmts) {
  // CB-152b: a standalone MI reaching finalize IS a closed singleton cycle,
  // and the documented default row for singletons is E2
  // (ProductDefaultRowID). Post-RA's hazard recognizer commits members with
  // the OPEN-cycle S2-first fill preference — correct while the cycle might
  // still co-issue, but a lone e3_* member here would drag the BUNDLE row to
  // E96ThreeEntry against the default. Re-settle to the logical's preferred
  // E2-committable sibling when one exists (same exactSolveLateSingleton
  // authority as the bare path below; no second theory of legality). The
  // sibling shares the logical's operand signature, so the rewrite is a
  // plain setDesc-compatible member swap.
  {
    const StringRef CurName = TII.getName(MI.getOpcode());
    if (isGeneratedFormatEMemberName(CurName) &&
        !haydn::bundle::formatECompositeSlotIsE2(
            Fmts.getSlotKind(MI.getOpcode()))) {
      const std::string Logical =
          haydn::format_e::peelLogicalOpcodeName(CurName);
      // Logical NAME → logical OPCODE, built once from the alts-bearing
      // opcodes (the same ledger the solver consumes; golden logical names
      // are the TD def names verbatim).
      static llvm::StringMap<unsigned> LogicalByName;
      static std::once_flag Once;
      std::call_once(Once, [&TII, &Fmts] {
        for (unsigned Opc = 0, E = TII.getNumOpcodes(); Opc != E; ++Opc)
          if (Fmts.getAlternateInstsOpcode(Opc))
            LogicalByName[TII.getName(Opc)] = Opc;
      });
      auto It = LogicalByName.find(Logical);
      if (!Logical.empty() && It != LogicalByName.end()) {
        if (auto Resettled =
                haydn::bundle::exactSolveLateSingleton(It->second, Fmts)) {
          const unsigned NewMember = Resettled->MemberOpcodes.front();
          if (NewMember != MI.getOpcode() &&
              haydn::bundle::formatECompositeSlotIsE2(
                  Fmts.getSlotKind(NewMember)) &&
              memberDescCompatible(MI, NewMember, TII)) {
            rewriteFieldSlotToMember(MI, NewMember, TII);
            LLVM_DEBUG(dbgs()
                       << "HaydnFinalizeBundle: singleton row resettle → "
                       << TII.getName(MI.getOpcode())
                       << " (closed cycle prefers ProductDefaultRowID E2)\n");
            return true;
          }
        }
      }
    }
  }
  // Residual cycle-forming SET_HWLOOP / LOADI32 / Loop* stay for the
  // late-firewall ban. Do not setDesc them onto a Format E member.
  if (haydn::bundle::isResidualCycleFormingPseudo(MI.getOpcode()))
    return false;

  auto Cycle = haydn::bundle::commitLateProductCycle(MI.getOpcode(), Fmts);
  if (!Cycle || !Cycle->NeedsSetDesc)
    return false;
  const StringRef NewName = TII.getName(Cycle->MemberOpcode);
  if (isGeneratedFormatEMemberName(NewName)) {
    const StringRef CurName = TII.getName(MI.getOpcode());
    if (isWideResidualName(CurName) &&
        !isWideCutoverLogical(haydn::format_e::peelLogicalOpcodeName(CurName)))
      return false;
    if (!memberDescCompatible(MI, Cycle->MemberOpcode, TII))
      return false;
  }
  if (isGeneratedFormatEMemberName(NewName))
    rewriteFieldSlotToMember(MI, Cycle->MemberOpcode, TII);
  else
    MI.setDesc(TII.get(Cycle->MemberOpcode));
  LLVM_DEBUG(dbgs() << "HaydnFinalizeBundle: late setDesc "
                    << Cycle->LogicalOpcode << " → " << Cycle->MemberOpcode
                    << " (empty-cycle tryAdd; AIE materializeMultiOpcodeInstrs "
                       "peer)\n");
  return true;
}

/// Copy the earliest member DebugLoc onto a BUNDLE root that has none.
/// Peer: MachineInstrBundle.cpp:90-101 getDebugLoc (first loc with a non-zero
/// line, else the first DILocation) and Hexagon packetize-debug-loc.mir.
/// AsmPrinter iterates top-level MIs only, so DwarfDebug::beginInstruction
/// sees the BUNDLE root, not the children (DwarfDebug.cpp:2099).
bool propagateEarliestMemberDebugLoc(MachineInstr &Root) {
  if (!Root.isBundle() || Root.getDebugLoc())
    return false;

  DebugLoc DL;
  MachineBasicBlock::instr_iterator I = std::next(Root.getIterator());
  MachineBasicBlock::instr_iterator E = getBundleEnd(Root.getIterator());
  for (; I != E; ++I) {
    if (DebugLoc MemberDL = I->getDebugLoc()) {
      if (MemberDL.getLine() != 0) {
        Root.setDebugLoc(MemberDL);
        return true;
      }
      if (!DL)
        DL = MemberDL;
    }
  }
  if (!DL)
    return false;
  Root.setDebugLoc(DL);
  return true;
}

bool isPadNopOpcode(unsigned Opc) {
  return haydn::bundle::isPadNopOpcode(Opc);
}

bool isResidualFieldSlotOpcode(unsigned Opc, const TargetInstrInfo &TII) {
  StringRef Name = TII.getName(Opc);
  if (isPadNopOpcode(Opc))
    return false;
  for (StringRef Suf :
       {"_S0", "_S1", "_S2", "_LD_S0", "_LD_S1", "_LD_S2", "_M0S0LS",
        "_M0S1LS", "_M0S2LS", "_M1S0LS", "_M1S1LS", "_M1S2LS"}) {
    if (Name.ends_with(Suf))
      return true;
  }
  return false;
}

bool hasRelocatableOperand(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.explicit_operands()) {
    if (MO.isMBB() || MO.isGlobal() || MO.isSymbol() || MO.isMCSymbol() ||
        MO.isBlockAddress() || MO.isJTI() || MO.isCPI() || MO.isTargetIndex())
      return true;
  }
  return false;
}

/// WIDE residual (`ADDI32_W_S0`, `CSRW_W_S0`, `ORI32_W_S0`). Not `D_LDW_*`
/// (`LDW` is the mnemonic, not the WIDE marker). Compact generated members
/// print without `_w` and use a different operand class.
bool isWideResidualName(StringRef Name) {
  return Name.contains("_W_S") || Name.ends_with("_W");
}

/// Golden logicals whose reloc-bearing `_W` FieldSlots may take MemberId.
/// Branch/call members carry brtarget_wide_*/calltarget_wide_*. ALU RI20
/// members carry simm20_wide_abs/uimm20_wide_abs. SET_HWLOOP Off1/Off2 stay
/// uimm6/uimm12; getExprFixupKind maps those ops to HWLoopOff1/Off2.
/// CSRW members are catalog I8; reloc CSRW_W cutovers to that MemberId.
/// Encode binds FIXUP_HAYDN_CSR_UImm8 / R_HAYDN_CSR_UImm8, never NONE.
bool isWideCutoverLogical(StringRef Log) {
  return Log.equals_insensitive("JAL") || Log.equals_insensitive("JALR") ||
         Log.equals_insensitive("BEQ") || Log.equals_insensitive("BNE") ||
         Log.equals_insensitive("BGE") || Log.equals_insensitive("BGEU") ||
         Log.equals_insensitive("BLT") || Log.equals_insensitive("BLTU") ||
         Log.equals_insensitive("BEQZ") || Log.equals_insensitive("BNEZ") ||
         Log.equals_insensitive("BGEZ") || Log.equals_insensitive("BLTZ") ||
         Log.equals_insensitive("ADDI32") || Log.equals_insensitive("ORI32") ||
         Log.equals_insensitive("ANDI32") || Log.equals_insensitive("XORI32") ||
         Log.equals_insensitive("SET_HWLOOP") ||
         Log.equals_insensitive("SET_HWLOOP_F2") ||
         Log.equals_insensitive("CSRW");
}

/// LS RI6 fields are signed 6-bit (FieldSlot LD32 uses wider simm16).
/// ALU RI6 fields are unsigned 6-bit (shifts). Cutover only when the
/// immediate fits the generated field.
bool ri6ImmFitsMember(const MachineInstr &MI,
                      const haydn::format_e::FormatEMemberRec &Mem) {
  if (StringRef(Mem.TypeName) != "RI6")
    return true;
  const bool SignedLS =
      Mem.Unit == static_cast<uint8_t>(haydn::format_e::FormatEUnit::LOADSTORE0) ||
      Mem.Unit == static_cast<uint8_t>(haydn::format_e::FormatEUnit::LOAD1);
  for (const MachineOperand &MO : MI.explicit_operands()) {
    if (!MO.isImm())
      continue;
    const int64_t V = MO.getImm();
    if (SignedLS) {
      if (V < -32 || V > 31)
        return false;
    } else if (V < 0 || V > 63) {
      return false;
    }
  }
  return true;
}

/// True when \p Opc must become a generated Format E member under the
/// committed row. FieldSlots and catalog logicals with a golden alt span
/// both resolve; pad NOP is CompletionState. Leaving a logical beside a
/// member is what forced residual `_S*` to exist — child order + Mode
/// retry replace that.
bool mustResolveToFormatEMember(unsigned Opc, const TargetInstrInfo &TII) {
  if (isPadNopOpcode(Opc))
    return false;
  const StringRef Name = TII.getName(Opc);
  if (isGeneratedFormatEMemberName(Name))
    return false;
  if (isResidualFieldSlotOpcode(Opc, TII))
    return true;
  const std::string Log = haydn::format_e::peelLogicalOpcodeName(Name);
  return haydn::format_e::findAltSpan(Log.c_str()) != nullptr;
}

/// True when residual FieldSlot, peel-rewritten alias, or unsuffixed
/// catalog logical \p MI may take generated member \p Mem (Desc, TIED_TO,
/// RI6, reloc policy). Entry is not consulted — the caller already chose
/// Mem from the ledger. SET_HWLOOP peel-identity stays verifier-banned.
bool fieldSlotCompatibleWithMember(
    const MachineInstr &MI, const haydn::format_e::FormatEMemberRec &Mem,
    const TargetInstrInfo &TII) {
  const unsigned Opc = MI.getOpcode();
  const StringRef Name = TII.getName(Opc);
  if (isPadNopOpcode(Opc))
    return false;
  if (isGeneratedFormatEMemberName(Name)) {
    // Already a private member: allow Mode/entry rebind of the same logical.
    const std::string Log = haydn::format_e::peelLogicalOpcodeName(Name);
    if (Mem.MemberId >= FormatEMemberOpcodeCount)
      return false;
    if (!StringRef(Mem.Logical).equals_insensitive(Log))
      return false;
    const unsigned MemberOpc = FormatEMemberOpcodes[Mem.MemberId];
    if (MemberOpc == 0 || TII.getName(MemberOpc).equals_insensitive("NOP"))
      return false;
    if (MemberOpc == Opc)
      return true;
    return memberDescCompatible(MI, MemberOpc, TII) &&
           ri6ImmFitsMember(MI, Mem);
  }
  if (Name.equals_insensitive("SET_HWLOOP") ||
      Name.equals_insensitive("SET_HWLOOP_REG") ||
      Name.equals_insensitive("SET_HWLOOP_F2"))
    return false;
  const std::string Log = haydn::format_e::peelLogicalOpcodeName(Name);
  if (hasRelocatableOperand(MI) && isWideResidualName(TII.getName(Opc)) &&
      !isWideCutoverLogical(Log))
    return false;
  if (Mem.MemberId >= FormatEMemberOpcodeCount)
    return false;
  if (!StringRef(Mem.Logical).equals_insensitive(Log))
    return false;
  const unsigned MemberOpc = FormatEMemberOpcodes[Mem.MemberId];
  if (MemberOpc == 0 || MemberOpc == Opc ||
      TII.getName(MemberOpc).equals_insensitive("NOP"))
    return false;
  if (!memberDescCompatible(MI, MemberOpc, TII))
    return false;
  return ri6ImmFitsMember(MI, Mem);
}

/// Resolve residual FieldSlot `_S*` to the generated MemberId for
/// (logical, row mode, membership entry). Entry is composite position, not
/// the suffix digit. Compact reloc (BEQZ_S0 / JAL_S0 MBB/global) cutovers:
/// the emitter assigns WIDE/LS/LO20 fixups from the peeled logical.
/// Reloc-bearing branch/call `_W` cutovers when the member carries the
/// WIDE PCRel class. Reloc-bearing ADDI32_W/ORI32_W cutover with
/// simm20_wide_abs/uimm20_wide_abs. Reloc SET_HWLOOP*_W cutover; Off1/Off2
/// stay uimm6/uimm12 and getExprFixupKind maps HWLoopOff by OpNo.
/// POST/PRE/BREV cutover when the member TIED_TO map matches FieldSlot.
/// Unsuffixed catalog logicals (`CSRW_W`, `ST32_POST`) use the same path.
/// Reloc CSRW_W cutovers to the CSRW member (same keep-map as immediate);
/// encode binds typed CSR I8 (FIXUP_HAYDN_CSR_UImm8 / R_HAYDN_CSR_UImm8).
/// Nullptr = fail closed.
const haydn::format_e::FormatEMemberRec *
resolveFieldSlotMember(const MachineInstr &MI, uint8_t Mode, uint8_t EntryIdx,
                       uint32_t UsedUnits, const TargetInstrInfo &TII) {
  const std::string Log =
      haydn::format_e::peelLogicalOpcodeName(TII.getName(MI.getOpcode()));
  // AIEHazardRecognizer.cpp:191-218 tries every AlternateInsts opcode
  // until canAdd. Overlay: walk generated members at (mode, entry) and
  // keep the lowest-UnitMap candidate the FieldSlot keep-map accepts.
  // findFormatEMember is UnitMap-min without operand proof; a 3-op ALU2
  // sibling must not hide a compatible 2-op ALU0 (CSRW I8 reloc/imm).
  const haydn::format_e::FormatEMemberRec *Best = nullptr;
  for (unsigned I = 0; I < haydn::format_e::FormatEMemberCount; ++I) {
    const haydn::format_e::FormatEMemberRec &M =
        haydn::format_e::FormatEMembers[I];
    if (M.IsNop || M.Mode != Mode || M.EntryIdx != EntryIdx)
      continue;
    if (!StringRef(Log).equals_insensitive(M.Logical))
      continue;
    if (M.Unit < 32 && (UsedUnits & (1u << M.Unit)))
      continue;
    if (!fieldSlotCompatibleWithMember(MI, M, TII))
      continue;
    if (!Best || M.UnitMap < Best->UnitMap)
      Best = &M;
  }
  return Best;
}

/// FieldSlot→MemberId on one stamped BUNDLE. Leading membership order is
/// tried first (e0, e1, …). Suffix digits never pin entries. Packs that
/// cannot bind in that order use assignFormatEMemberEntries (store only at
/// E3 e0 LOADSTORE0; dual loads LS0+LOAD1). Transactional: every residual
/// `_S*` child must resolve, else the bundle stays FieldSlot. Mixed
/// MemberId + leftover FieldSlot does not DFS — fail closed. Pad NOP/NOP_S0
/// is CompletionState (unused-entry zero NOP), not a unit and not
/// membership — skip without consuming EntryIdx. After a successful
/// rewrite, erase co-issued pads so AsmPrinter never mixes MemberId with
/// leftover FieldSlot NOP (pre-finalize NOP_S0 is occupancy, not a
/// member). Singleton `{ NOP_S0 }`
/// stays; that is the idle parcel, not pad beside real work.
bool cutoverBundleFieldSlots(MachineInstr &Root, const TargetInstrInfo &TII) {
  if (!Root.isBundle())
    return false;
  auto Row = haydn::bundle::getBundleRowID(Root);
  if (!Row)
    return false;
  const uint8_t Mode =
      *Row == haydn::bundle::BundleFormatRowID::E96ThreeEntry ? 1 : 0;
  const unsigned EntryCap = haydn::bundle::bundleRowEntryCount(*Row);

  SmallVector<MachineInstr *, 4> Kids;
  SmallVector<MachineInstr *, 4> PadNops;
  MachineBasicBlock::instr_iterator I = std::next(Root.getIterator());
  MachineBasicBlock::instr_iterator E = getBundleEnd(Root.getIterator());
  for (; I != E; ++I) {
    // Same non-member skip as the shared census: meta/debug/position
    // children are not encode membership (collectBundleMemberOpcodes skips
    // exactly this triple; ANNOTATION_LABEL is isPosition but not isMeta,
    // so a meta-only skip here would fork the count).
    if (I->isMetaInstruction() || I->isDebugInstr() || I->isPosition())
      continue;
    // Same pad law as the shared census (isPadNopOpcode): pad NOP children
    // are CompletionState, never membership. The census itself lives in
    // HaydnBundleVerify.cpp; this partition must not fork it, so the
    // predicate is the shared one, and counts derived below cross-check it.
    if (haydn::bundle::isPadNopOpcode(I->getOpcode())) {
      PadNops.push_back(&*I);
      continue;
    }
    Kids.push_back(&*I);
  }
  const unsigned NonPadKids = Kids.size();
  // W28/CR-B3 one-census cross-check: the cutover partition and the shared
  // verifier census must agree on (member count, pad presence) for this
  // root. Divergence means someone re-introduced a second membership law —
  // fail closed rather than stamping an unverifiable completion.
  assert((NonPadKids ==
              haydn::bundle::collectBundleMemberOpcodes(Root).size() &&
          PadNops.empty() == !haydn::bundle::bundleHasPadNop(Root)) &&
         "cutover pad/membership partition forked the shared census law");

  auto applyPlan =
      [&](ArrayRef<std::pair<MachineInstr *,
                             const haydn::format_e::FormatEMemberRec *>>
              Plan) -> bool {
    if (Plan.empty() && (PadNops.empty() || NonPadKids == 0))
      return false;
    for (auto [MI, Mem] : Plan) {
      const unsigned MemberOpc = FormatEMemberOpcodes[Mem->MemberId];
      rewriteFieldSlotToMember(*MI, MemberOpc, TII);
    }
    // Erase only when real work remains. Completion AllEntriesReal is
    // unchanged for any MemberCount > 0. Also strips pad beside already-
    // member children so MC never sees MemberId + leftover NOP_S0.
    if (NonPadKids > 0) {
      for (MachineInstr *Nop : PadNops)
        Nop->eraseFromBundle();
    }
    return true;
  };

  using PlanItem =
      std::pair<MachineInstr *, const haydn::format_e::FormatEMemberRec *>;
  SmallVector<PlanItem, 4> Plan;

  // AIEFinalizeBundle.cpp:49-56 is identity on already-bundled roots.
  // Haydn overlay: when every real child is already a generated member and
  // the independent inverse accepts the stamp, late Finalize must not
  // rewrite members or E2/E3 restamp. Pad NOP beside real work is still
  // CompletionState and is erased (shared census). Residual FieldSlot
  // children and inverse-rejected stale stamps fall through to bind below.
  bool AllPrivateMembers = !Kids.empty();
  for (MachineInstr *Kid : Kids) {
    if (!isGeneratedFormatEMemberName(TII.getName(Kid->getOpcode()))) {
      AllPrivateMembers = false;
      break;
    }
  }
  if (AllPrivateMembers) {
    HaydnMCFormats Fmts;
    if (!haydn::bundle::verifyCommittedBundle(Root, Fmts))
      return applyPlan(Plan);
  }

  uint32_t UsedUnits = 0;
  unsigned EntryIdx = 0;
  bool LeadingFailed = false;
  bool AnyMember = false;
  bool AnyMustResolve = false;
  for (MachineInstr *Kid : Kids) {
    const unsigned KidOpc = Kid->getOpcode();
    if (isGeneratedFormatEMemberName(TII.getName(KidOpc))) {
      AnyMember = true;
      if (EntryIdx >= EntryCap) {
        LeadingFailed = true;
        break;
      }
      if (const haydn::format_e::FormatEMemberRec *Priv =
              haydn::bundle::lookupPrivateFormatEMember(KidOpc)) {
        if (Priv->Mode == Mode &&
            Priv->EntryIdx == static_cast<uint8_t>(EntryIdx)) {
          if (Priv->Unit < 32) {
            if (UsedUnits & (1u << Priv->Unit)) {
              LeadingFailed = true;
              break;
            }
            UsedUnits |= (1u << Priv->Unit);
          }
          ++EntryIdx;
          continue;
        }
      }
      const haydn::format_e::FormatEMemberRec *Mem = resolveFieldSlotMember(
          *Kid, Mode, static_cast<uint8_t>(EntryIdx), UsedUnits, TII);
      if (!Mem) {
        // Already-member at the wrong entry (ADDI32 e0 + ST e0) — Mode-retry
        // assign, do not keep two e0 members under a sequential walk.
        LeadingFailed = true;
        break;
      }
      const unsigned NewOpc = FormatEMemberOpcodes[Mem->MemberId];
      if (NewOpc != KidOpc && memberDescCompatible(*Kid, NewOpc, TII))
        Plan.emplace_back(Kid, Mem);
      if (Mem->Unit < 32)
        UsedUnits |= (1u << Mem->Unit);
      ++EntryIdx;
      continue;
    }
    const bool MustResolve = mustResolveToFormatEMember(KidOpc, TII);
    if (MustResolve)
      AnyMustResolve = true;
    // Bundle child order is the composite operand order (e0, e1, …).
    // Do not require an `_S*` postfix to attempt MemberId — AIE has no
    // suffix peel; PacketFormats + InstSlot choose the encoding.
    if (EntryIdx >= EntryCap) {
      if (MustResolve) {
        LeadingFailed = true;
        break;
      }
      ++EntryIdx;
      continue;
    }
    const haydn::format_e::FormatEMemberRec *Mem = resolveFieldSlotMember(
        *Kid, Mode, static_cast<uint8_t>(EntryIdx), UsedUnits, TII);
    // resolveFieldSlotMember answers (logical, mode, entry, units) only; it
    // does not prove the MI's operand shape can carry the member desc. Since
    // members may carry ties the FieldSlot MI lacks (accumulator ties landed
    // by the member-mirrors-tie work), require the same compatibility the
    // ledger-rebind path checks before planning the rewrite — otherwise an
    // incompatible pair reaches rewriteFieldSlotToMember and trips its
    // memberDescCompatible contract assert.
    if (Mem && (!memberDescCompatible(*Kid, FormatEMemberOpcodes[Mem->MemberId],
                                      TII) ||
                !ri6ImmFitsMember(*Kid, *Mem)))
      Mem = nullptr;
    if (!Mem) {
      if (MustResolve) {
        LeadingFailed = true;
        break;
      }
    } else {
      Plan.emplace_back(Kid, Mem);
      if (Mem->Unit < 32)
        UsedUnits |= (1u << Mem->Unit);
    }
    ++EntryIdx;
  }
  if (!LeadingFailed)
    return applyPlan(Plan);

  // Inverse already accepts this stamp: identity. Do not E2/E3 restamp a
  // verified root (late PreEmit re-entry). Still apply any FieldSlot
  // rewrites collected before the sequential failure, and drop pad beside
  // real work. Residual FieldSlot-only packs and inverse-rejected stale
  // stamps (E2 child-count over E3-only two-ALU32) Mode-retry below.
  if (AnyMember) {
    HaydnMCFormats Fmts;
    if (!haydn::bundle::verifyCommittedBundle(Root, Fmts))
      return applyPlan(Plan);
  }

  // Membership order could not bind every child. Residual FieldSlot-only
  // or stale-stamp packs rebind via the generated ledger. Placement owns
  // row+entry; MC does not Mode-retry.
  if (!AnyMustResolve && !AnyMember)
    return false;
  SmallVector<std::string, 3> Logs;
  Logs.reserve(Kids.size());
  for (MachineInstr *Kid : Kids)
    Logs.push_back(
        haydn::format_e::peelLogicalOpcodeName(TII.getName(Kid->getOpcode())));

  auto planFromAssign =
      [&](uint8_t TryMode) -> std::optional<SmallVector<PlanItem, 4>> {
    if (Kids.size() > haydn::bundle::bundleRowEntryCount(
            TryMode ? haydn::bundle::BundleFormatRowID::E96ThreeEntry
                    : haydn::bundle::BundleFormatRowID::E96TwoEntry))
      return std::nullopt;
    auto Assign =
        haydn::format_e::assignFormatEMemberEntries(Logs, TryMode);
    if (!Assign)
      return std::nullopt;
    SmallVector<PlanItem, 4> P;
    uint32_t PlanUnits = 0;
    for (unsigned K = 0, KE = Kids.size(); K != KE; ++K) {
      const haydn::format_e::FormatEMemberRec *Mem = (*Assign)[K].Mem;
      if (!Mem)
        return std::nullopt;
      MachineInstr *Kid = Kids[K];
      // Assigned UnitMap-min may be operand-incompatible (CSRW ALU2 3-op vs
      // catalog 2-op). Retry siblings at the same entry
      // (AIEHazardRecognizer.cpp:216-218 alt-try). Dual LOADSTORE0 stores
      // have no second unit — fail closed rather than skip a kid and mix
      // MemberId with leftover logicals.
      if (!fieldSlotCompatibleWithMember(*Kid, *Mem, TII) ||
          (Mem->Unit < 32 && (PlanUnits & (1u << Mem->Unit)))) {
        uint32_t Used = PlanUnits;
        for (unsigned J = K + 1; J != KE; ++J) {
          if (!(*Assign)[J].Mem || (*Assign)[J].Mem->Unit >= 32)
            continue;
          Used |= (1u << (*Assign)[J].Mem->Unit);
        }
        Mem = resolveFieldSlotMember(*Kid, TryMode, Mem->EntryIdx, Used, TII);
        if (!Mem)
          return std::nullopt;
      }
      if (Mem->Unit < 32) {
        if (PlanUnits & (1u << Mem->Unit))
          return std::nullopt;
        PlanUnits |= (1u << Mem->Unit);
      }
      P.emplace_back(Kid, Mem);
    }
    if (P.size() != Kids.size())
      return std::nullopt;
    return P;
  };

  // Stamped row is identity when it can bind. Child-count E2↔E3 override
  // of a stamped root is a silent repair — refuse it (printer/verify fatal
  // "exceeds stamped row"). An illegal same-count stamp (XOR32/BEQZ at E2
  // entry 1 — both e0-only under E2) is not verified: rematch onto the
  // other product row from the generated ledger, then restamp that row.
  const unsigned StampedCap = haydn::bundle::bundleRowEntryCount(
      Mode ? haydn::bundle::BundleFormatRowID::E96ThreeEntry
           : haydn::bundle::BundleFormatRowID::E96TwoEntry);
  if (Kids.size() > StampedCap)
    return false;
  auto P = planFromAssign(Mode);
  uint8_t UseMode = Mode;
  if (!P) {
    HaydnMCFormats Fmts;
    if (!haydn::bundle::verifyCommittedBundle(Root, Fmts))
      return false;
    const uint8_t Other = Mode ? 0 : 1;
    P = planFromAssign(Other);
    if (!P)
      return false;
    UseMode = Other;
  }
  if (UseMode != Mode) {
    const auto NewRow =
        UseMode ? haydn::bundle::BundleFormatRowID::E96ThreeEntry
                : haydn::bundle::BundleFormatRowID::E96TwoEntry;
    const auto Comp = haydn::bundle::selectCompletionForMembersAndPads(
        NewRow, Kids.size(), !PadNops.empty());
    assert(haydn::bundle::isProductLegalCompletion(Comp) &&
           "Mode restamp must keep full-slot product completion");
    haydn::bundle::stampBundleCommit(Root, NewRow, Comp);
  }
  return applyPlan(*P);
}

} // namespace

bool llvm::haydnRecommitLateMixedBare(MachineFunction &MF) {
  // Wrap leftover bare MIs only. Do not restamp already-bundled roots —
  // a missing BundleFormatRowID must still fatal at the printer.
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  HaydnMCFormats Fmts;

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::instr_iterator MII = MBB.instr_begin();
    MachineBasicBlock::instr_iterator MIE = MBB.instr_end();
    if (MII == MIE)
      continue;
    assert(!MII->isInsideBundle() && "First instr cannot be inside bundle!");

    // Wrap leftover bare *real* encode MIs only (LUI/ADDI32_W/JALR_W after
    // BranchRelaxation). Do not wrap BUNDLE roots (missing-row stays fatal)
    // or representation-expand pseudos (B/RET/BR_JT stay printer-owned).
    while (MII != MIE) {
      if (!MII->isInsideBundle() && isBundleCandidate(MII)) {
        if (materializeLateBareIfNeeded(*MII, TII, Fmts))
          Changed = true;
        finalizeBundle(MBB, MII, std::next(MII));
        // MII still points at the original MI (now a BUNDLE child).
        MachineInstr &Root = *getBundleStart(MII);
        assert(Root.isBundle() && "finalizeBundle must produce a BUNDLE root");
        // Durable Format E row + completion. Late solve supplies residual
        // setDesc member identity; completion is always forced to full-slot
        // architectural NOP pad (AllEntriesReal) for the single real member
        // — refuse to stamp unqualified singleton-stub underfill invent.
        // W28/CR-B3 (encoding F11): the completion value comes from the SAME
        // shared pad census the verifier replays (collectBundleMemberOpcodes
        // + bundleHasPadNop + selectCompletionForMembersAndPads), computed on
        // the just-wrapped root. A wrapped bare NOP/NOP_S0 is a pad-only
        // idle cycle (0 reals + pad), not a 1-real member cycle — both
        // select AllEntriesReal today, but one route means the stamp can
        // never diverge from the verify expectation.
        SmallVector<unsigned, 3> WrappedMembers =
            haydn::bundle::collectBundleMemberOpcodes(Root);
        const bool WrappedHasPad = haydn::bundle::bundleHasPadNop(Root);
        if (auto Late =
                haydn::bundle::commitLateProductCycle(MII->getOpcode(), Fmts)) {
          Late->Plan.Completion = haydn::bundle::selectCompletionForMembersAndPads(
              Late->Plan.Row, WrappedMembers.size(), WrappedHasPad);
          assert(haydn::bundle::isProductLegalCompletion(Late->Plan.Completion) &&
                 "late singleton commit must be full-slot product completion");
          haydn::bundle::stampBundleCommit(Root, Late->Plan);
        } else {
          haydn::bundle::BundlePlan Plan = haydn::bundle::makeProductPlan(
              /*Occupied=*/0, {MII->getOpcode()});
          Plan.Completion = haydn::bundle::selectCompletionForMembersAndPads(
              Plan.Row, WrappedMembers.size(), WrappedHasPad);
          assert(haydn::bundle::isProductLegalCompletion(Plan.Completion) &&
                 "singleton product plan must be full-slot completion");
          haydn::bundle::stampBundleCommit(Root, Plan);
        }
        if (cutoverBundleFieldSlots(Root, TII))
          Changed = true;
        Changed = true;
      }
      ++MII;
    }
  }
  return Changed;
}

bool HaydnFinalizeBundle::runOnMachineFunction(MachineFunction &MF) {
  // Commit ownership is not quality: never call skipFunction. Generic
  // PostMachineScheduler still skips optnone (no reorder); this pass is the
  // target-local no-reorder owner that wraps each remaining bare MI as a
  // Format E singleton cycle so product emission never sees uncommitted MIR.
  // Plain O0 without optnone still runs postmisched first and may already
  // hold multi-MI full-fill packs (idempotent: already-bundled roots skip).
  // optnone lands here with standalone MIs only. Non-empty singleton cycles
  // always stamp full-slot architectural NOP pad (AllEntriesReal): unused
  // entry windows encode as zero-entry NOP — not unqualified underfill or
  // singleton-stub invent. Product emission must never carry noncanonical
  // underfilled stub completion into MC.

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  bool Changed = haydnRecommitLateMixedBare(MF);

  // Already-bundled residual roots (hand MIR / exact-commit) may have only
  // a row imm or no completion. Unstamped roots get a product row from
  // member count so cutover can bind FieldSlots; already-stamped roots
  // are left to the inverse. Without this stamp, cutover skips residual
  // FieldSlot+logical children.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (!MI.isBundle())
        continue;
      SmallVector<unsigned, 3> Members =
          haydn::bundle::collectBundleMemberOpcodes(MI);
      const bool HasPadNop = haydn::bundle::bundleHasPadNop(MI);
      auto Row = haydn::bundle::getBundleRowID(MI);
      const bool HasCompletion = haydn::bundle::getBundleCompletionID(MI).has_value();
      if (Row && HasCompletion)
        continue;
      if (!Row) {
        // Missing-row: FieldSlot children need a row so cutover can bind.
        // Pad-only idle is a legal full-bundle NOP parcel (printer !Row→E2
        // is fatal). Do not invent an E2 default from child count for
        // mixed logicals — cutover rematches those from the ledger.
        bool SawFieldSlot = false;
        if (const MachineBasicBlock *P = MI.getParent()) {
          for (MachineBasicBlock::const_instr_iterator I =
                   std::next(MI.getIterator());
               I != P->instr_end() && I->isBundledWithPred(); ++I) {
            if (isResidualFieldSlotOpcode(I->getOpcode(), TII)) {
              SawFieldSlot = true;
              break;
            }
          }
        }
        const bool PadOnlyIdle = Members.empty() && HasPadNop;
        if (!SawFieldSlot && !PadOnlyIdle)
          continue;
        Row = haydn::bundle::selectProductRowForOpcodes(Members);
      }
      // Row-only roots get the missing completion from the shared census.
      // W28/CR-B3: one law with verify (collect + pad + select).
      haydn::bundle::stampBundleCommit(
          MI, *Row,
          haydn::bundle::selectCompletionForMembersAndPads(
              *Row, Members.size(), HasPadNop));
      Changed = true;
    }
  }

  // FieldSlot→MemberId: composite membership position owns the entry, not
  // the `_S*` suffix digit. Runs over newly wrapped and already-bundled
  // roots (the wrap loop skips the latter). Fail closed leaves residuals
  // whose operand shape does not match the generated member Desc.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (cutoverBundleFieldSlots(MI, TII))
        Changed = true;
    }
  }

  // Product commit tail: fill empty BUNDLE-root DebugLoc from the earliest
  // member. Generic finalizeBundle already copies loc on new wraps; this
  // covers already-bundled roots the wrap loop skips (multi-MI packs from
  // post-RA exact commit).
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (propagateEarliestMemberDebugLoc(MI))
        Changed = true;
    }
  }

  return Changed;
}

char HaydnFinalizeBundle::ID = 0;

INITIALIZE_PASS(HaydnFinalizeBundle, DEBUG_TYPE, "Haydn Bundle Finalization",
                false, false)

HaydnFinalizeBundle::HaydnFinalizeBundle() : MachineFunctionPass(ID) {
  initializeHaydnFinalizeBundlePass(*PassRegistry::getPassRegistry());
}

void HaydnFinalizeBundle::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

FunctionPass *llvm::createHaydnFinalizeBundlePass() {
  return new HaydnFinalizeBundle();
}
