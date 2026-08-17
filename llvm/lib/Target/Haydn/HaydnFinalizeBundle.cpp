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
//     Residual SET_HWLOOP (peel-identity) stays for the verifier ban. Reloc CSRW_W
//     stays FieldSlot (no typed CSR fixup). Pad NOP/NOP_S0 is
//     CompletionState, not membership — skip without consuming an entry and
//     erase co-issued pads after a successful rewrite. Product print is the
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
  // Already-bundled MIs (BUNDLE roots with BundledSucc, or children).
  if (MI->isMetaInstruction() || MII->isBundled())
    return false;
  // Inline asm is a TargetOpcode pseudo lowered by AsmPrinter::emitInlineAsm
  // on the top-level MI. Wrapping it as a BUNDLE child drops the APP block
  // (BUNDLE path skips non-SETCBR pseudos). Leave standalone.
  if (MI->isInlineAsm())
    return false;
  return true;
}

bool isWideResidualName(StringRef Name);
bool isWideCutoverLogical(StringRef Log);

/// Keep-list from FieldSlot explicit operands onto generated member Desc.
/// Closed drop rules (no bag-sort by register class):
///  * identity when counts match
///  * drop every ins TIED_TO a def (MAC/MOVT seed-copy acc; dual-dest MAC
///    drops two acc ins)
///  * trailing extra uses when NumDefs matches and no ins is tied to a def
///    (MOVE32/ABS32 vestigial rs2)
///  * skip first ins when NumDefs match and trailing drop fails
///    (LUI vestigial $rs between dest and imm)
/// LUI/ZERO_GPR cutover requires generated dest as SSA out (catalog role
/// `reg`). Skip-Finalize / hand-asm FieldSlot still bag-sorts.
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

  if (OldN == NewN && OldDesc.getNumDefs() == NewDesc.getNumDefs()) {
    SmallVector<unsigned, 4> Keep;
    for (unsigned I = 0; I != NewN; ++I) {
      if (!kindOk(I, I))
        return std::nullopt;
      Keep.push_back(I);
    }
    if (!tiesOk(Keep))
      return std::nullopt;
    return Keep;
  }

  // Drop ins that are TIED_TO a def (MAC/MOVT seed-copy accumulators).
  // Remaining explicit operands must match the member. Do this before
  // trailing-use drop so dual-dest MAC (rtd1,rtd2,rta1,rta2,rsd1,rsd2)
  // keeps sources, not the tied acc pair.
  if (OldDesc.getNumDefs() == NewDesc.getNumDefs() && OldN > NewN) {
    SmallVector<unsigned, 4> Keep;
    bool DroppedTied = false;
    for (unsigned I = 0; I != OldN; ++I) {
      const int Tie = OldDesc.getOperandConstraint(I, MCOI::TIED_TO);
      if (Tie >= 0 && static_cast<unsigned>(Tie) < OldDesc.getNumDefs()) {
        DroppedTied = true;
        continue;
      }
      Keep.push_back(I);
    }
    if (DroppedTied && Keep.size() == NewN) {
      bool Ok = true;
      for (unsigned NewI = 0; Ok && NewI != NewN; ++NewI)
        Ok = kindOk(Keep[NewI], NewI);
      if (Ok && tiesOk(Keep))
        return Keep;
    }
  }

  // Trailing extra uses: prefix matches member; extras are not defs
  // (MOVE32/ABS32 vestigial rs2). Skip when any ins is tied to a def —
  // that is the seed-copy case above.
  if (OldDesc.getNumDefs() == NewDesc.getNumDefs() && OldN > NewN) {
    bool AnyTiedUse = false;
    for (unsigned I = OldDesc.getNumDefs(); I != OldN; ++I) {
      const int Tie = OldDesc.getOperandConstraint(I, MCOI::TIED_TO);
      if (Tie >= 0 && static_cast<unsigned>(Tie) < OldDesc.getNumDefs()) {
        AnyTiedUse = true;
        break;
      }
    }
    if (!AnyTiedUse) {
      SmallVector<unsigned, 4> Keep;
      bool PrefixOk = true;
      for (unsigned I = 0; I != NewN; ++I) {
        if (!kindOk(I, I)) {
          PrefixOk = false;
          break;
        }
        Keep.push_back(I);
      }
      bool TrailingUses = true;
      for (unsigned I = NewN; I != OldN; ++I) {
        if (I < OldDesc.getNumDefs()) {
          TrailingUses = false;
          break;
        }
      }
      if (PrefixOk && TrailingUses && tiesOk(Keep))
        return Keep;
    }
  }

  // Vestigial first ins (LUI $rs): NumDefs match, one extra use that is
  // not trailing (dest, rs, imm) → (dest, imm). Only after trailing-drop
  // fails so MOVE32 (dest, rs, rs2) keeps the prefix, not rs2.
  if (OldDesc.getNumDefs() == NewDesc.getNumDefs() && OldN == NewN + 1) {
    const unsigned Defs = OldDesc.getNumDefs();
    if (Defs >= 1 && Defs < NewN) {
      SmallVector<unsigned, 4> Keep;
      bool Ok = true;
      for (unsigned I = 0; Ok && I != Defs; ++I) {
        Ok = kindOk(I, I);
        Keep.push_back(I);
      }
      for (unsigned NewI = Defs; Ok && NewI != NewN; ++NewI) {
        const unsigned OldI = NewI + 1;
        if (OldI >= OldN) {
          Ok = false;
          break;
        }
        Ok = kindOk(OldI, NewI);
        Keep.push_back(OldI);
      }
      if (Ok && Keep.size() == NewN && tiesOk(Keep))
        return Keep;
    }
  }

  return std::nullopt;
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

/// setDesc to \p MemberOpc and drop FieldSlot operands the member does not
/// keep. Caller already proved memberDescCompatible.
void llvm::rewriteFieldSlotToMember(MachineInstr &MI, unsigned MemberOpc,
                                    const TargetInstrInfo &TII) {
  const MCInstrDesc &OldDesc = MI.getDesc();
  const MCInstrDesc &NewDesc = TII.get(MemberOpc);
  auto Keep = fieldSlotKeepOperands(MI, NewDesc);
  assert(Keep && "rewriteFieldSlotToMember requires memberDescCompatible");
  SmallVector<unsigned, 4> DropTies;
  for (unsigned NewI = 0, NewE = NewDesc.getNumOperands(); NewI != NewE;
       ++NewI) {
    const unsigned OldI = (*Keep)[NewI];
    if (OldDesc.getOperandConstraint(OldI, MCOI::TIED_TO) != -1 &&
        NewDesc.getOperandConstraint(NewI, MCOI::TIED_TO) == -1)
      DropTies.push_back(NewI);
  }
  const unsigned OldN = OldDesc.getNumOperands();
  for (unsigned I = OldN; I > 0; --I) {
    const unsigned OpI = I - 1;
    if (!is_contained(*Keep, OpI))
      MI.removeOperand(OpI);
  }
  MI.setDesc(NewDesc);
  for (unsigned OpI : DropTies)
    MI.untieRegOperand(OpI);
}

namespace {

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
         Log.equals_insensitive("SET_HWLOOP_F2");
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
/// Reloc CSRW_W is not a WIDE-cutover logical (no typed CSR fixup).
/// Nullptr = fail closed.
const haydn::format_e::FormatEMemberRec *
resolveFieldSlotMember(const MachineInstr &MI, uint8_t Mode, uint8_t EntryIdx,
                       uint32_t UsedUnits, const TargetInstrInfo &TII) {
  const std::string Log =
      haydn::format_e::peelLogicalOpcodeName(TII.getName(MI.getOpcode()));
  const haydn::format_e::FormatEMemberRec *Mem =
      haydn::format_e::findFormatEMember(Log, Mode, EntryIdx, UsedUnits);
  if (!Mem || !fieldSlotCompatibleWithMember(MI, *Mem, TII))
    return nullptr;
  return Mem;
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
/// leftover FieldSlot NOP (Bundle.canAdd is the packetizer oracle and must
/// still treat pre-finalize NOP_S0 as occupancy). Singleton `{ NOP_S0 }`
/// stays; that is the idle parcel, not pad beside real work.
bool cutoverBundleFieldSlots(MachineInstr &Root, const TargetInstrInfo &TII) {
  if (!Root.isBundle() || Root.getNumOperands() < 2 ||
      !Root.getOperand(0).isImm() || !Root.getOperand(1).isImm())
    return false;
  auto Row = haydn::bundle::formatRowFromImm(
      static_cast<unsigned>(Root.getOperand(0).getImm()));
  if (!Row)
    return false;
  const uint8_t Mode =
      *Row == haydn::bundle::BundleFormatRowID::E96ThreeEntry ? 1 : 0;
  const unsigned EntryCap = Mode ? 3u : 2u;

  SmallVector<MachineInstr *, 4> Kids;
  SmallVector<MachineInstr *, 4> PadNops;
  MachineBasicBlock::instr_iterator I = std::next(Root.getIterator());
  MachineBasicBlock::instr_iterator E = getBundleEnd(Root.getIterator());
  for (; I != E; ++I) {
    if (I->isMetaInstruction())
      continue;
    if (isPadNopOpcode(I->getOpcode())) {
      PadNops.push_back(&*I);
      continue;
    }
    Kids.push_back(&*I);
  }
  const unsigned NonPadKids = Kids.size();

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

  // Membership order could not bind every child. Rebind via the generated
  // ledger (same DFS as standalone hand-asm), including already-members so
  // a stale E2 stamp can restamp E3 when two ALU32s only cover E3.
  // Placement owns row+entry; MC does not Mode-retry.
  if (!AnyMustResolve && !AnyMember)
    return false;
  SmallVector<std::string, 3> Logs;
  Logs.reserve(Kids.size());
  for (MachineInstr *Kid : Kids)
    Logs.push_back(
        haydn::format_e::peelLogicalOpcodeName(TII.getName(Kid->getOpcode())));

  auto planFromAssign =
      [&](uint8_t TryMode) -> std::optional<SmallVector<PlanItem, 4>> {
    if (Kids.size() > (TryMode ? 3u : 2u))
      return std::nullopt;
    auto Assign =
        haydn::format_e::assignFormatEMemberEntries(Logs, TryMode);
    if (!Assign)
      return std::nullopt;
    SmallVector<PlanItem, 4> P;
    for (unsigned K = 0, KE = Kids.size(); K != KE; ++K) {
      const haydn::format_e::FormatEMemberRec *Mem = (*Assign)[K].Mem;
      if (!Mem)
        return std::nullopt;
      MachineInstr *Kid = Kids[K];
      const bool MustResolve =
          mustResolveToFormatEMember(Kid->getOpcode(), TII) ||
          isGeneratedFormatEMemberName(TII.getName(Kid->getOpcode()));
      if (!fieldSlotCompatibleWithMember(*Kid, *Mem, TII)) {
        if (MustResolve)
          return std::nullopt;
        continue;
      }
      P.emplace_back(Kid, Mem);
    }
    return P;
  };

  uint8_t UseMode = Mode;
  auto P = planFromAssign(Mode);
  if (!P) {
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
    const auto Comp =
        haydn::bundle::selectCompletionFor(NewRow, Kids.size());
    assert(haydn::bundle::isProductLegalCompletion(Comp) &&
           "Mode restamp must keep full-slot product completion");
    haydn::bundle::stampBundleCommit(Root, NewRow, Comp);
  }
  return applyPlan(*P);
}

} // namespace

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
  HaydnMCFormats Fmts;

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::instr_iterator MII = MBB.instr_begin();
    MachineBasicBlock::instr_iterator MIE = MBB.instr_end();
    if (MII == MIE)
      continue;
    assert(!MII->isInsideBundle() && "First instr cannot be inside bundle!");

    // Port of AIEFinalizeBundle.cpp:49-56: wrap each standalone candidate.
    // setDesc first when empty-cycle tryAdd selects a format member
    // (late bare NOP/BR/demote inserts after PreEmit growth).
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
        if (auto Late =
                haydn::bundle::commitLateProductCycle(MII->getOpcode(), Fmts)) {
          Late->Plan.Completion = haydn::bundle::selectCompletionFor(
              Late->Plan.Row, /*MemberCount=*/1);
          assert(haydn::bundle::isProductLegalCompletion(Late->Plan.Completion) &&
                 "late singleton commit must be full-slot product completion");
          haydn::bundle::stampBundleCommit(Root, Late->Plan);
        } else {
          haydn::bundle::BundlePlan Plan = haydn::bundle::makeProductPlan(
              /*Occupied=*/0, {MII->getOpcode()});
          Plan.Completion = haydn::bundle::selectCompletionFor(
              Plan.Row, /*MemberCount=*/1);
          assert(haydn::bundle::isProductLegalCompletion(Plan.Completion) &&
                 "singleton product plan must be full-slot completion");
          haydn::bundle::stampBundleCommit(Root, Plan);
        }
        Changed = true;
      }
      ++MII;
    }
  }

  // Already-bundled residual roots (hand MIR / exact-commit) may have only
  // a row imm or no completion. Child count picks E2/E3; cutover then
  // Mode-retries. Without this stamp, cutover skips and verify canAdd of
  // FieldSlot+logical fails.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (!MI.isBundle())
        continue;
      if (MI.getNumOperands() >= 2 && MI.getOperand(0).isImm() &&
          MI.getOperand(1).isImm() &&
          haydn::bundle::formatRowFromImm(
              static_cast<unsigned>(MI.getOperand(0).getImm())))
        continue;
      unsigned N = 0;
      bool HasPadNop = false;
      MachineBasicBlock::instr_iterator I = std::next(MI.getIterator());
      MachineBasicBlock::instr_iterator E = getBundleEnd(MI.getIterator());
      for (; I != E; ++I) {
        if (I->isMetaInstruction())
          continue;
        if (isPadNopOpcode(I->getOpcode())) {
          HasPadNop = true;
          continue;
        }
        ++N;
      }
      const auto Row = N >= 3 ? haydn::bundle::BundleFormatRowID::E96ThreeEntry
                              : haydn::bundle::BundleFormatRowID::E96TwoEntry;
      haydn::bundle::stampBundleCommit(
          MI, Row,
          haydn::bundle::selectCompletionForMembersAndPads(Row, N, HasPadNop));
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
