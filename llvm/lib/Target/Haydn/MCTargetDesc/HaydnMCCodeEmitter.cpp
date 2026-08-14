//===-- HaydnMCCodeEmitter.cpp - Haydn Code Emitter interface ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the HaydnMCCodeEmitter class.
//
// Product encoding profile is Format E (96-bit / registry EncodedBytes).
// Live product composites are BUNDLE_E96_TWO_ENTRY / BUNDLE_E96_THREE_ENTRY
// (generated field geometry + InstBits header indicator 111). FE8: the legacy
// composite opcode and encode APIs are deleted — zero residual product emit.
// Empty/idle parcels fail closed until golden idle is registered.
//
// AIE peers (serialize-only model for Format E entry composition):
//   AIEBaseMCCodeEmitter.cpp:45-68  encodeInstruction = getBinaryCode + emit
//   AIEBaseMCCodeEmitter.cpp:122-184 encode nested sub-inst from member Desc
//
// `encodeInstruction` routing (serialize-only / fail-closed):
//   BUNDLE_E96_* product      -> one-parcel joint place + emit; never multi-parcel
//   Haydn::BUNDLE residual     -> encodeBundle → same one-parcel Format E path
//   PseudoLongB*              -> expandLongBranch (recurse encodeInstruction)
//   standalone real opcode    -> wrap as Format E E2 singleton (+ NOP underfill)
// Placement failure fails closed — no sequential E2 singleton split (layout-size
// fiction vs BranchRelaxation / FixupHwLoops). Residual FieldSlot composites
// serialize as-is / Mode / one dual swap only; they never free-DFS. Bounded
// one-parcel DFS is standalone hand-asm only (golden logical names). Compiler
// 3-child rebind (store at E3 e0 LOADSTORE0; dual loads LS0+LOAD1) lives in
// Finalize assignFormatEMemberEntries. FieldSlot suffixes never pin entries.
// encodeSlotSubInst is serialize-only: every non-NOP child must already be a
// generated private member. Name peel and operand rebuild stay in placement
// (standalone DFS + residual FieldSlot fillFormatEMemberInst), never in
// entry encode.
//
//===----------------------------------------------------------------------===//

#include "HaydnMCCodeEmitter.h"
#include "HaydnFormatERecords.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFixupKinds.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <optional>
#include <string>

#define DEBUG_TYPE "haydn-mccodeemitter"

using namespace llvm;
using namespace llvm::haydn::format_e;

namespace {

// Hexagon parity: PC-rel is a property of the *fixup kind*, set at
// MCFixup::create time — not via getFixupKindInfo Flags (FKF_IsPCRel is gone
// in this LLVM; Hexagon/RISCV leave Flags=0 and switch on kind here).
// Mirrors HexagonMCCodeEmitter.cpp `addFixup`.
static bool isHaydnPCRelFixupKind(unsigned Kind) {
  switch (Kind) {
  case Haydn::FIXUP_HAYDN_CallSImm20:
  case Haydn::FIXUP_HAYDN_WIDE_CallSImm20:
  case Haydn::FIXUP_HAYDN_BranchSImm16:
  case Haydn::FIXUP_HAYDN_WIDE_BranchSImm12:
  case Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI:
  case Haydn::FIXUP_HAYDN_LongBranchSImm20:
  case Haydn::FIXUP_HAYDN_C_BranchSImm4:
  case Haydn::FIXUP_HAYDN_C_BranchSImm10:
  case Haydn::FIXUP_HAYDN_32_PCREL:
  case Haydn::FIXUP_HAYDN_PC_LO20:
  case Haydn::FIXUP_HAYDN_HWLoopOffset:
  case Haydn::FIXUP_HAYDN_HWLoopOff1:
  case Haydn::FIXUP_HAYDN_HWLoopOff2:
    return true;
  default:
    return false;
  }
}

static void addHaydnFixup(SmallVectorImpl<MCFixup> &Fixups, uint32_t Offset,
                          const MCExpr *Value, unsigned Kind) {
  Fixups.push_back(MCFixup::create(Offset, Value,
                                   static_cast<MCFixupKind>(Kind),
                                   isHaydnPCRelFixupKind(Kind)));
}

/// Map `%hi12/%lo20/%pc_lo20` MCSpecifierExpr to the existing Haydn fixup
/// kind. Bare symbols return nullopt so the opcode switch remains the default.
static std::optional<unsigned> fixupKindFromSpecifier(const MCExpr *Expr) {
  const auto *SE = dyn_cast_or_null<MCSpecifierExpr>(Expr);
  if (!SE)
    return std::nullopt;
  switch (SE->getSpecifier()) {
  case ELF::R_HAYDN_HI12:
    return Haydn::FIXUP_HAYDN_HI12;
  case ELF::R_HAYDN_LO20:
    return Haydn::FIXUP_HAYDN_LO20;
  case ELF::R_HAYDN_PC_LO20:
    return Haydn::FIXUP_HAYDN_PC_LO20;
  default:
    return std::nullopt;
  }
}

// Forward decls for Format E placement (defined with encodeSlotSubInst).
static std::string formatELogicalName(StringRef Name);
static const FormatEMemberRec *
findFormatEMember(StringRef Logical, uint8_t Mode, uint8_t EntryIdx,
                  uint32_t UsedUnitMask = 0);
static const FormatEMemberRec *findFormatEMemberByOpcode(unsigned Opc);
static bool isFormatENopOpcode(unsigned Opc, const MCInstrInfo &MII);
static bool isResidualSlotMemberOpcode(unsigned Opc, const MCInstrInfo &MII);
/// Rebuild \p In as one Format E parcel with golden entry assignment.
/// Prefer sequential as-is serialize for committed BUNDLE_E96_* rows; residual
/// bare logicals place within one row only (no E2↔E3 upgrade on committed
/// composites). Returns false if no injective one-parcel assignment exists
/// (caller must fail closed — never multi-parcel).
static bool buildFormatEPlacedComposite(const MCInst &In,
                                        const MCInstrInfo &MII,
                                        const MCRegisterInfo &MRI, MCInst &Out,
                                        SmallVectorImpl<MCInst> &Storage);

//===----------------------------------------------------------------------===//
// HaydnMCCodeEmitter
//===----------------------------------------------------------------------===//

class HaydnMCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;
  const MCInstrInfo &MII;

public:
  HaydnMCCodeEmitter(MCContext &Ctx, const MCInstrInfo &MII)
      : Ctx(Ctx), MII(MII) {}
  ~HaydnMCCodeEmitter() override = default;

  // P2b — APInt 5-param overload (AIE-model). Format E 96-bit composites
  // force -gen-emitter to emit this signature.
  void getBinaryCodeForInstr(const MCInst &MI, SmallVectorImpl<MCFixup> &Fixups,
                             APInt &Inst, APInt &Scratch,
                             const MCSubtargetInfo &STI) const;

  void getMachineOpValue(const MCInst &MI, const MCOperand &MO, APInt &Op,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;

  void getBranchTargetOpValue(const MCInst &MI, unsigned OpNo, APInt &Op,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;

  void getCallTargetOpValue(const MCInst &MI, unsigned OpNo, APInt &Op,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  // AIE-style scaled-immediate encoder for logical *_W / *_dr / hwloop_off*
  // operand classes (Format E encode). Bound via tablegen EncoderMethod.
  template <unsigned N, unsigned Shift, bool IsSigned, bool IsPCRel,
            Haydn::Fixups FixupKind>
  void getSImmOpValueXStepWide(const MCInst &MI, unsigned OpNo, APInt &Op,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;

  void encodeInstruction(const MCInst &Inst, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

private:
  // Expand a long branch pseudo to: inverted-conditional-branch + JAL.
  // Each emitted real instruction recurses through `encodeInstruction`.
  void expandLongBranch(const MCInst &MI, SmallVectorImpl<char> &CB,
                        SmallVectorImpl<MCFixup> &Fixups,
                        const MCSubtargetInfo &STI) const;

  // AIE-model slot sub-instruction encoding (AIEBaseMCCodeEmitter.cpp:122-184).
  // Always encodes SubInst member Desc as-is. Post-RA setDesc
  // (AIEMachineScheduler materializeMultiOpcodeInstrs) is the sole
  // materialize authority.
  void encodeSlotSubInst(const MCInst &Composite, const MCInst &SubInst,
                         APInt &Op, SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;

  // Residual TargetOpcode::BUNDLE: pack children as product Format E parcels
  // (E2/E3 placement + registry EncodedBytes). Empty/all-NOP uses canonical
  // idle when registered.
  void encodeBundle(const MCInst &MBI, SmallVectorImpl<char> &CB,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const;

  unsigned getBranchFixupKind(const MCInst &MI) const;
  unsigned getCallFixupKind(const MCInst &MI) const;
};

} // end anonymous namespace

/// True when \p Mem / peeled \p Logical is a Format E LOADSTORE0/LOAD1 RI6
/// member (signed imm6 @ bits[33:28] → FIXUP_HAYDN_LS_IMM). ALU RI6 (SRAI64
/// etc.) and ALU RI20 / retired WIDE LSOff20 (LO20) stay out.
static bool isFormatELSRI6(const FormatEMemberRec *Mem, StringRef Logical) {
  auto isLSUnit = [](uint8_t Unit) {
    return Unit == static_cast<uint8_t>(FormatEUnit::LOADSTORE0) ||
           Unit == static_cast<uint8_t>(FormatEUnit::LOAD1);
  };
  if (Mem && Mem->TypeName && StringRef(Mem->TypeName) == "RI6" &&
      isLSUnit(Mem->Unit))
    return true;
  if (Logical.empty())
    return false;
  for (unsigned I = 0; I < FormatEMemberCount; ++I) {
    const FormatEMemberRec &M = FormatEMembers[I];
    if (M.IsNop || !M.Logical || !M.TypeName)
      continue;
    if (!Logical.equals_insensitive(M.Logical))
      continue;
    if (StringRef(M.TypeName) == "RI6" && isLSUnit(M.Unit))
      return true;
  }
  return false;
}

// Determine the appropriate fixup kind for an expression operand.
// MCSpecifierExpr (%hi12/%lo20/%pc_lo20) selects the kind; the opcode
// switch is the bare-symbol default (RISCVMCCodeEmitter.cpp getImmOpValue).
// Geometry per HaydnRelocLayout.
// Slot-variant / Format E member opcodes (post-setDesc or encode Wire fill)
// route symbolic operands through getMachineOpValue — peel to logical name
// so LUI_E2_E0_ALU0_I12 gets HI12 (not default FIXUP_HAYDN_32 that clobbers
// Format E indicator → linked objdump <unknown>).
// OpNo is required for multi-field SET_HWLOOP Off1/Off2 (two distinct kinds
// on one instruction); pass ~0u when the operand index is unknown.
static unsigned getExprFixupKind(const MCInst &MI, const MCInstrInfo &MII,
                                 unsigned OpNo = ~0u,
                                 const MCExpr *Expr = nullptr) {
  if (std::optional<unsigned> SpecKind = fixupKindFromSpecifier(Expr))
    return *SpecKind;

  // Typed committed private members: take Logical from generated MemberId
  // record (no opcode-name peel / marker recovery).
  const FormatEMemberRec *Typed = findFormatEMemberByOpcode(MI.getOpcode());
  std::string LogicalStorage;
  if (Typed && Typed->Logical && Typed->Logical[0] != '\0')
    LogicalStorage = Typed->Logical;
  if (LogicalStorage.empty())
    LogicalStorage = formatELogicalName(MII.getName(MI.getOpcode()));
  const StringRef Logical = LogicalStorage;

  // Format E SET_HWLOOP / SET_HWLOOP_F2 members use plain uimm6/uimm12 for
  // Off1/Off2 (no hwloop_off* EncoderMethod). Symbolic start/end labels must
  // emit typed HWLoopOff1/Off2 — never FIXUP_HAYDN_32, which post-link
  // clobbers the Format E parcel into <unknown>.
  // Operand order: sel, offset1, offset2, cnt|rs.
  if (Logical.equals_insensitive("SET_HWLOOP") ||
      Logical.equals_insensitive("SET_HWLOOP_F2") ||
      Logical.equals_insensitive("SET_HWLOOP_W") ||
      Logical.equals_insensitive("SET_HWLOOP_F2_W")) {
    if (OpNo == 1)
      return Haydn::FIXUP_HAYDN_HWLoopOff1;
    if (OpNo == 2)
      return Haydn::FIXUP_HAYDN_HWLoopOff2;
    // Unknown OpNo: still refuse Data32 — Off1 is the safer typed default
    // only when a single expr is mis-indexed; prefer Off1 over clobber.
    return Haydn::FIXUP_HAYDN_HWLoopOff1;
  }
  // Match peeled logical name first (covers all E2/E3/S* members).
  if (Logical.equals_insensitive("LUI"))
    return Haydn::FIXUP_HAYDN_HI12;
  if (Logical.equals_insensitive("ADDI32") ||
      Logical.equals_insensitive("ORI32") ||
      Logical.equals_insensitive("ANDI32") ||
      Logical.equals_insensitive("XORI32") ||
      Logical.equals_insensitive("ADDI32_W") ||
      Logical.equals_insensitive("ORI32_W"))
    return Haydn::FIXUP_HAYDN_LO20;
  if (Logical.equals_insensitive("ADDI32S") ||
      Logical.equals_insensitive("SUBI32") ||
      Logical.equals_insensitive("SUBI32S"))
    return Haydn::FIXUP_HAYDN_LO16;
  // Format E JAL I20: golden imm @ parcel bits[31:50] → WIDE_CallSImm20.
  // Legacy CallSImm20 (FieldLsb=4) corrupts the Format E header/map on link.
  if (Logical.equals_insensitive("JAL"))
    return Haydn::FIXUP_HAYDN_WIDE_CallSImm20;
  if (Logical.equals_insensitive("JALR"))
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  // One-reg I12 branches.
  if (Logical.equals_insensitive("BEQZ") ||
      Logical.equals_insensitive("BNEZ") ||
      Logical.equals_insensitive("BLTZ") ||
      Logical.equals_insensitive("BGEZ") ||
      Logical.equals_insensitive("BEQZ_W") ||
      Logical.equals_insensitive("BNEZ_W") ||
      Logical.equals_insensitive("BGEZ_W") ||
      Logical.equals_insensitive("BLTZ_W"))
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12;
  // Two-reg RI12 branches.
  if (Logical.equals_insensitive("BEQ") ||
      Logical.equals_insensitive("BNE") ||
      Logical.equals_insensitive("BGE") ||
      Logical.equals_insensitive("BGEU") ||
      Logical.equals_insensitive("BLT") ||
      Logical.equals_insensitive("BLTU") ||
      Logical.equals_insensitive("BEQ_W") ||
      Logical.equals_insensitive("BNE_W") ||
      Logical.equals_insensitive("BGE_W") ||
      Logical.equals_insensitive("BGEU_W") ||
      Logical.equals_insensitive("BLT_W") ||
      Logical.equals_insensitive("BLTU_W"))
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;

  // Narrow LS RI6 (S_LW_WITH_IMM / LD32 peel / generated members): LS_IMM.
  // Wide LSOff20 / ALU RI20 already returned LO20 above (ADDI32/ORI32).
  // RISCV split: InstFormatI → lo12_i vs InstFormatS → lo12_s vs CI reject
  // (RISCVMCCodeEmitter.cpp:640-643, RISCVELFObjectWriter.cpp:134-140).
  if (isFormatELSRI6(Typed, Logical))
    return Haydn::FIXUP_HAYDN_LS_IMM;

  // Kind is the peeled / MemberId logical above. Residual `_S*` FieldSlots
  // and generated members must not grow a second opcode switch.
  return Haydn::FIXUP_HAYDN_32;
}

//===----------------------------------------------------------------------===//
// Top-level encode dispatch — Format E product profile
//===----------------------------------------------------------------------===//

void HaydnMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                           SmallVectorImpl<char> &CB,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  // Live Format E product composites: generated InstBits + entry fields,
  // then little-endian registry EncodedBytes (12) via haydnEmitFormatEParcelLE.
  if (MI.getOpcode() == Haydn::BUNDLE_E96_TWO_ENTRY ||
      MI.getOpcode() == Haydn::BUNDLE_E96_THREE_ENTRY) {
    // Fail closed when every entry is NOP — product idle completion is not
    // registered yet (all-zero / header-only is not a legal claim).
    bool AnyReal = false;
    for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
      const MCOperand &Op = MI.getOperand(I);
      if (!Op.isInst() || !Op.getInst())
        continue;
      if (!isFormatENopOpcode(Op.getInst()->getOpcode(), MII)) {
        AnyReal = true;
        break;
      }
    }
    if (!AnyReal) {
      SmallVector<char, 16> Idle;
      if (haydnTryGetCanonicalIdleParcel(Idle)) {
        CB.append(Idle.begin(), Idle.end());
        return;
      }
      report_fatal_error(
          "Haydn MC: empty Format E composite has no product-approved idle/"
          "completion parcel — refuse all-zero pad",
          /*GenCrashDiag=*/false);
    }

    // One compiler cycle → one E96 parcel. Joint place residual bare logicals
    // into the committed row when needed; never split into sequential E2
    // singletons (that rewrote layout size after BranchRelaxation / FixupHwLoops).
    SmallVector<MCInst, 4> PlaceStorage;
    MCInst Placed;
    auto emitOneComposite = [&](const MCInst &Comp) {
      APInt InstBits, Scratch;
      SmallVector<MCFixup, 8> LocalFixups;
      getBinaryCodeForInstr(Comp, LocalFixups, InstBits, Scratch, STI);
      haydn::format::EncodedBits ProdBits = haydn::format::encodedBitsOrDie(
          haydn::format::BundleFormatRowID::E96TwoEntry);
      APInt Word96 = InstBits.zextOrTrunc(ProdBits.Value);
      if ((Word96.extractBitsAsZExtValue(3, 0) & 0x7u) !=
          haydn::format::FormatEIndicatorBits) {
        report_fatal_error(
            "Haydn MC: Format E parcel missing indicator 111 after encode",
            /*GenCrashDiag=*/false);
      }
      const uint32_t Base = static_cast<uint32_t>(CB.size());
      for (const MCFixup &F : LocalFixups)
        addHaydnFixup(Fixups, Base + F.getOffset(), F.getValue(), F.getKind());
      haydnEmitFormatEParcelLE(Word96, CB);
    };

    if (buildFormatEPlacedComposite(MI, MII, *Ctx.getRegisterInfo(), Placed,
                                    PlaceStorage)) {
      emitOneComposite(Placed);
      return;
    }

    SmallVector<const MCInst *, 3> Reals;
    std::string ChildDiag;
    for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
      const MCOperand &Op = MI.getOperand(I);
      if (!Op.isInst() || !Op.getInst())
        continue;
      unsigned ChildOpc = Op.getInst()->getOpcode();
      StringRef RawName = MII.getName(ChildOpc);
      std::string Log = formatELogicalName(RawName);
      if (!ChildDiag.empty())
        ChildDiag += "; ";
      ChildDiag += RawName.str();
      ChildDiag += "→";
      ChildDiag += Log.empty() ? "<empty>" : Log;
      if (isFormatENopOpcode(ChildOpc, MII) || Log.empty() ||
          StringRef(Log).equals_insensitive("NOP"))
        continue;
      Reals.push_back(Op.getInst());
    }
    if (Reals.empty()) {
      report_fatal_error(
          Twine("Haydn MC: Format E composite placement failed with no reals "
                "after filter; children=[") +
              ChildDiag + "]",
          /*GenCrashDiag=*/false);
    }
    // Fail closed: refuse multi-parcel sequential E2 singleton emit. Post-RA
    // owns the exact one-parcel commit; MC only serializes. Typed private
    // Format E members never enter standalone DFS / name-recovery re-place.
    // Residual FieldSlot composites never free-DFS; reaching here means
    // one-parcel as-is / Mode / swap placement failed entirely.
    bool HasPrivate = false;
    for (const MCInst *C : Reals) {
      if (findFormatEMemberByOpcode(C->getOpcode())) {
        HasPrivate = true;
        break;
      }
    }
    if (HasPrivate) {
      report_fatal_error(
          Twine("Haydn MC: committed Format E private member cannot enter "
                "standalone DFS re-place; children=[") +
              ChildDiag +
              "] — refuse name recovery / row retry (serialize-only)",
          /*GenCrashDiag=*/false);
    }
    report_fatal_error(
        Twine("Haydn MC: Format E one-parcel placement failed for composite "
              "with ") +
            Twine(static_cast<unsigned>(Reals.size())) +
            " real child(ren) [" + ChildDiag +
            "] — refuse sequential E2 singleton split (serialize-only)",
        /*GenCrashDiag=*/false);
  }

  // Residual TargetOpcode::BUNDLE (generic producers).
  if (MI.getOpcode() == Haydn::BUNDLE) {
    encodeBundle(MI, CB, Fixups, STI);
    return;
  }

  // Long-branch pseudos — expand to inverted-conditional-branch + JAL.
  switch (MI.getOpcode()) {
  default:
    break;
  case Haydn::PseudoLongBEQ:
  case Haydn::PseudoLongBNE:
  case Haydn::PseudoLongBGE:
  case Haydn::PseudoLongBGEU:
  case Haydn::PseudoLongBLT:
  case Haydn::PseudoLongBLTU:
  case Haydn::PseudoLongBEQZ:
  case Haydn::PseudoLongBNEZ:
  case Haydn::PseudoLongBGEZ:
  case Haydn::PseudoLongBLTZ:
  case Haydn::PseudoLongB:
    expandLongBranch(MI, CB, Fixups, STI);
    return;
  }

  // Standalone real opcodes (hand-asm bare logicals / residual producers):
  // wrap as Format E two-entry composite (real + NOP underfill). Matches
  // AsmParser braced single-op emit and product EncodedBytes. Idle bare NOP
  // remains fail-closed until golden idle is registered.
  if (MI.getOpcode() == Haydn::NOP) {
    SmallVector<char, 16> Idle;
    if (haydnTryGetCanonicalIdleParcel(Idle)) {
      CB.append(Idle.begin(), Idle.end());
      return;
    }
    report_fatal_error(
        "Haydn MC: bare NOP has no product-approved Format E idle parcel",
        /*GenCrashDiag=*/false);
  }

  MCInst Nop;
  Nop.setOpcode(Haydn::NOP);
  MCInst Comp;
  Comp.setOpcode(Haydn::BUNDLE_E96_TWO_ENTRY);
  Comp.addOperand(MCOperand::createInst(&MI));
  Comp.addOperand(MCOperand::createInst(&Nop));
  encodeInstruction(Comp, CB, Fixups, STI);
}
//===----------------------------------------------------------------------===//
// Bundle encoding — fail-closed product idle / residual reject
//===----------------------------------------------------------------------===//

void HaydnMCCodeEmitter::encodeBundle(const MCInst &MBI,
                                      SmallVectorImpl<char> &CB,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  // Collect real children (NOP / empty catalog names are underfill, not reals).
  SmallVector<const MCInst *, 4> Children;
  for (unsigned I = 0, E = MBI.getNumOperands(); I != E; ++I) {
    const MCOperand &Op = MBI.getOperand(I);
    if (!Op.isInst() || !Op.getInst())
      continue;
    const MCInst *Child = Op.getInst();
    if (isFormatENopOpcode(Child->getOpcode(), MII))
      continue;
    Children.push_back(Child);
  }

  // Empty / all-NOP: product idle must be a full Format E parcel with
  // indicator 111. All-zero is not Format E.
  if (Children.empty()) {
    SmallVector<char, 16> Idle;
    if (haydnTryGetCanonicalIdleParcel(Idle)) {
      CB.append(Idle.begin(), Idle.end());
      return;
    }
    report_fatal_error(
        "Haydn MC: empty bundle has no product-approved Format E idle/"
        "completion parcel — refuse all-zero pad",
        /*GenCrashDiag=*/false);
  }

  // Wrap residual BUNDLE children as one Format E composite and reuse the
  // product BUNDLE_E96_* encode path (one-parcel joint place; fail closed).
  // Stack NOP underfill (const method cannot allocate via MCContext).
  MCInst Pad0, Pad1, Pad2;
  Pad0.setOpcode(Haydn::NOP);
  Pad1.setOpcode(Haydn::NOP);
  Pad2.setOpcode(Haydn::NOP);
  MCInst *Pads[3] = {&Pad0, &Pad1, &Pad2};
  MCInst Comp;
  const unsigned N = static_cast<unsigned>(Children.size());
  Comp.setOpcode(N >= 3 ? Haydn::BUNDLE_E96_THREE_ENTRY
                        : Haydn::BUNDLE_E96_TWO_ENTRY);
  for (const MCInst *C : Children)
    Comp.addOperand(MCOperand::createInst(C));
  const unsigned EntryCount = N >= 3 ? 3u : 2u;
  for (unsigned K = N; K < EntryCount; ++K)
    Comp.addOperand(MCOperand::createInst(Pads[K]));
  encodeInstruction(Comp, CB, Fixups, STI);
}

//===----------------------------------------------------------------------===//
// encodeSlotSubInst — serialize one placed Format E entry (generated emitter)
//===----------------------------------------------------------------------===//
//
// AIEBaseMCCodeEmitter.cpp:134-162 encodes SubInst Desc as-is;
// SubInstFormat/slot geometry come from SubInst.getOpcode().
//
// Placement (buildFormatEPlacedComposite) emits generated private members
// or NOP. This function serializes that Desc. FieldSlot / bare logical
// children are a placement bug — fail closed, no name peel or rebuild.
// MemberId → Haydn::<E96 member opcode> (generated with live TD members).
// File scope so typed serialize/lookup and fillFormatEMemberInst share one map.
#define GET_FORMAT_E_MEMBER_OPCODES
#include "HaydnGenFormatEMemberOpcodes.inc"

// File-scope forward decl so as-is residual cutover can call the wire
// rebuild defined later in this TU (outside the placement helpers ns).
// encodeSlotSubInst never calls this — placement only.
static bool fillFormatEMemberInst(const FormatEMemberRec &Mem,
                                  const MCInst &Logical, const MCInstrInfo &MII,
                                  const MCRegisterInfo &MRI, MCInst &Out);

// Same TU anonymous namespace as the emitter class (C++ merges them).
namespace {

// Residual hand-asm: matcher may still match the logical public mnemonic
// (ADD32 before ADD32_S*). Materialize that residual via sparse
// getAlternateInstsOpcode[SlotIdx] — the same PlacementAlternative / setDesc
// member table. Local copy only; no MCFlags.
static bool isFormatENopOpcode(unsigned Opc, const MCInstrInfo &MII) {
  if (Opc == Haydn::NOP)
    return true;
  std::string Log = formatELogicalName(MII.getName(Opc));
  // Empty catalog name is not a product real (unknown pseudo / meta).
  // Covers residual NOP_S0 and generated NOP_* members without naming _S*.
  return Log.empty() || StringRef(Log).equals_insensitive("NOP");
}

// Resolve a Format E placement member for (logical, mode, entry_idx). Prefer
// lower UnitMap among candidates whose Unit is not in UsedUnitMask.
static const FormatEMemberRec *findFormatEMember(StringRef Logical, uint8_t Mode,
                                                 uint8_t EntryIdx,
                                                 uint32_t UsedUnitMask) {
  if (Logical.empty() || Logical.equals_insensitive("NOP"))
    return nullptr;
  const FormatEMemberRec *Fallback = nullptr;
  for (unsigned I = 0; I < FormatEMemberCount; ++I) {
    const FormatEMemberRec &M = FormatEMembers[I];
    if (M.IsNop || M.Mode != Mode || M.EntryIdx != EntryIdx)
      continue;
    if (!Logical.equals_insensitive(M.Logical))
      continue;
    if (M.Unit < 32 && (UsedUnitMask & (1u << M.Unit)))
      continue;
    // Prefer lower UnitMap for deterministic choice among legal units.
    if (!Fallback || M.UnitMap < Fallback->UnitMap)
      Fallback = &M;
  }
  return Fallback;
}

/// Typed reverse map: private Format E member opcode → generated MemberId.
/// Non-NOP private members are unique in FormatEMemberOpcodes; NOP multi-maps
/// and is excluded (caller treats product NOP as zero entry).
static const FormatEMemberRec *findFormatEMemberByOpcode(unsigned Opc) {
  if (Opc == 0 || Opc == Haydn::NOP)
    return nullptr;
  for (unsigned I = 0; I < FormatEMemberOpcodeCount; ++I) {
    if (FormatEMemberOpcodes[I] != Opc)
      continue;
    if (I >= FormatEMemberCount)
      return nullptr;
    const FormatEMemberRec &M = FormatEMembers[I];
    if (M.IsNop)
      return nullptr;
    return &M;
  }
  return nullptr;
}

/// Residual PacketFormats format-member opcodes (slot suffix peers).
static bool isResidualSlotMemberOpcode(unsigned Opc, const MCInstrInfo &MII) {
  if (Opc == 0 || isFormatENopOpcode(Opc, MII))
    return false;
  if (findFormatEMemberByOpcode(Opc))
    return false;
  StringRef Name = MII.getName(Opc);
  for (StringRef Suf :
       {"_S0", "_S1", "_S2", "_LD_S0", "_LD_S1", "_LD_S2", "_M0S0LS",
        "_M0S1LS", "_M0S2LS", "_M1S0LS", "_M1S1LS", "_M1S2LS"}) {
    if (Name.ends_with(Suf))
      return true;
  }
  return false;
}

/// Serialize-as-is when every real is fixed at its composite entry under
/// \p Mode (0=E2, 1=E3):
///  * typed private Format E member (MemberId + Mode/EntryIdx), or
///  * residual FieldSlot member placeable at that composite entry by logical
///    name only, cut over to the generated private MemberId wire when the
///    residual operand shape matches (positional copy or bag-sort rebuild).
///    FieldSlot _S0/_S1/_S2 suffixes never supply Format-E entry indices.
/// Positional full-slot packs with zero-entry NOP pads serialize as-is.
/// Private members never DFS. Residual soft-falls back when wire rebuild fails.
/// encodeSlotSubInst never peels or rebuilds.
/// When \p ForceMode is set, entry count and Out opcode follow ForceMode even
/// if In carries the other product composite (fixed-entry Mode-retry cutover).
static bool trySerializeFormatECompositeAsIs(const MCInst &In,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI,
                                             MCInst &Out,
                                             SmallVectorImpl<MCInst> &Storage,
                                             std::optional<uint8_t> ForceMode =
                                                 std::nullopt) {
  const bool IsE3 = In.getOpcode() == Haydn::BUNDLE_E96_THREE_ENTRY;
  const bool IsE2 = In.getOpcode() == Haydn::BUNDLE_E96_TWO_ENTRY;
  if (!IsE2 && !IsE3 && !ForceMode)
    return false;
  const uint8_t Mode =
      ForceMode ? *ForceMode : static_cast<uint8_t>(IsE3 ? 1 : 0);
  const unsigned EntryCount = Mode ? 3u : 2u;
  // Prefer explicit entry children; residual Mode-retry may pad from fewer
  // In operands when ForceMode upgrades E2→E3 (extra entries become NOP).
  if (!ForceMode && In.getNumOperands() < EntryCount)
    return false;
  if (ForceMode && In.getNumOperands() == 0)
    return false;

  auto isPrivateMemberAt = [&](unsigned Opc, unsigned Entry) -> bool {
    const FormatEMemberRec *Mem = findFormatEMemberByOpcode(Opc);
    if (!Mem)
      return false;
    return Mem->Mode == Mode && Mem->EntryIdx == static_cast<uint8_t>(Entry);
  };

  SmallVector<const MCInst *, 3> ChildAt(EntryCount, nullptr);
  SmallVector<const FormatEMemberRec *, 3> MemAt(EntryCount, nullptr);
  SmallVector<bool, 3> ResidualAt(EntryCount, false);
  unsigned RealCount = 0;
  uint32_t UsedUnits = 0;
  // Collect real children in composite order (skip NOP). ForceMode may
  // re-bind the same ordered reals onto a different entry count.
  SmallVector<const MCInst *, 3> OrderedReals;
  for (unsigned I = 0, N = In.getNumOperands(); I != N; ++I) {
    const MCOperand &Op = In.getOperand(I);
    if (!Op.isInst() || !Op.getInst())
      continue;
    const unsigned ChildOpc = Op.getInst()->getOpcode();
    if (isFormatENopOpcode(ChildOpc, MII))
      continue;
    OrderedReals.push_back(Op.getInst());
  }
  if (OrderedReals.empty() || OrderedReals.size() > EntryCount)
    return false;

  bool AllPrivate = true;
  for (const MCInst *Child : OrderedReals) {
    if (!findFormatEMemberByOpcode(Child->getOpcode())) {
      AllPrivate = false;
      break;
    }
  }

  // Typed MemberId composites serialize by committed EntryIdx, not operand
  // order. Holes are encode-time CompletionState NOP. Do not relocate a
  // member to a different entry than its opcode records.
  if (AllPrivate) {
    for (const MCInst *Child : OrderedReals) {
      const FormatEMemberRec *Mem = findFormatEMemberByOpcode(Child->getOpcode());
      if (!Mem || Mem->Mode != Mode || Mem->EntryIdx >= EntryCount)
        return false;
      const unsigned E = Mem->EntryIdx;
      if (ChildAt[E] || (Mem->Unit < 32 && (UsedUnits & (1u << Mem->Unit))))
        return false;
      if (Mem->Unit < 32)
        UsedUnits |= (1u << Mem->Unit);
      ChildAt[E] = Child;
      MemAt[E] = Mem;
      ResidualAt[E] = false;
      ++RealCount;
    }
    if (RealCount == 0)
      return false;
    Storage.clear();
    Storage.reserve(EntryCount);
    Out.clear();
    Out.setOpcode(Mode ? Haydn::BUNDLE_E96_THREE_ENTRY
                       : Haydn::BUNDLE_E96_TWO_ENTRY);
    for (unsigned E = 0; E < EntryCount; ++E) {
      if (!ChildAt[E]) {
        Storage.emplace_back();
        Storage.back().setOpcode(Haydn::NOP);
        Out.addOperand(MCOperand::createInst(&Storage.back()));
        continue;
      }
      Out.addOperand(MCOperand::createInst(ChildAt[E]));
    }
    return true;
  }

  // Residual FieldSlot: bind reals to leading entries in operand order;
  // trailing entries stay NOP. FieldSlot suffixes never pin Format-E entry.
  // 3-child residual that fails this as-is bind does not DFS here —
  // Finalize assignFormatEMemberEntries owns that rebind.
  for (unsigned E = 0; E < EntryCount; ++E) {
    if (E >= OrderedReals.size()) {
      ChildAt[E] = nullptr;
      continue;
    }
    const MCInst *Child = OrderedReals[E];
    const unsigned ChildOpc = Child->getOpcode();
    if (isPrivateMemberAt(ChildOpc, E)) {
      const FormatEMemberRec *Mem = findFormatEMemberByOpcode(ChildOpc);
      if (Mem->Unit < 32 && (UsedUnits & (1u << Mem->Unit)))
        return false;
      if (Mem->Unit < 32)
        UsedUnits |= (1u << Mem->Unit);
      ChildAt[E] = Child;
      MemAt[E] = Mem;
      ResidualAt[E] = false;
      ++RealCount;
      continue;
    }
    if (isResidualSlotMemberOpcode(ChildOpc, MII)) {
      std::string Log = formatELogicalName(MII.getName(ChildOpc));
      const FormatEMemberRec *Mem =
          findFormatEMember(Log, Mode, static_cast<uint8_t>(E), UsedUnits);
      if (!Mem)
        return false;
      if (Mem->Unit < 32)
        UsedUnits |= (1u << Mem->Unit);
      ChildAt[E] = Child;
      MemAt[E] = Mem;
      ResidualAt[E] = true;
      ++RealCount;
      continue;
    }
    return false; // bare logical — one-parcel DFS below
  }
  if (RealCount == 0)
    return false;

  Storage.clear();
  Storage.reserve(EntryCount);
  Out.clear();
  Out.setOpcode(Mode ? Haydn::BUNDLE_E96_THREE_ENTRY
                     : Haydn::BUNDLE_E96_TWO_ENTRY);
  for (unsigned E = 0; E < EntryCount; ++E) {
    if (!ChildAt[E]) {
      Storage.emplace_back();
      Storage.back().setOpcode(Haydn::NOP);
      Out.addOperand(MCOperand::createInst(&Storage.back()));
      continue;
    }
    if (!ResidualAt[E]) {
      Out.addOperand(MCOperand::createInst(ChildAt[E]));
      continue;
    }
    // Residual FieldSlot → generated private MemberId wire. Compiler
    // Finalize already drops extra FieldSlot ops (MOVE32 rs2, MAC acc,
    // LUI vestigial rs) when the member dest is an SSA out. Remaining
    // bag-sort: skip-Finalize / hand-asm FieldSlot. encodeSlotSubInst
    // never peels or rebuilds.
    Storage.emplace_back();
    if (!fillFormatEMemberInst(*MemAt[E], *ChildAt[E], MII, MRI,
                               Storage.back()))
      return false;
    Out.addOperand(MCOperand::createInst(&Storage.back()));
  }
  return true;
}

/// Place real children onto Format E entries with unit injectivity.
/// As-is / Mode / one dual swap first. Residual FieldSlot never free-DFS
/// (Finalize assignFormatEMemberEntries owns 3-child rebind). Typed private
/// members bind by committed EntryIdx, never DFS. Bare hand-asm is the
/// separately typed standalone entry and may one-parcel DFS.
static bool buildFormatEPlacedComposite(const MCInst &In,
                                        const MCInstrInfo &MII,
                                        const MCRegisterInfo &MRI, MCInst &Out,
                                        SmallVectorImpl<MCInst> &Storage) {
  if (trySerializeFormatECompositeAsIs(In, MII, MRI, Out, Storage))
    return true;

  // Typed private Format E members must serialize as-is (EntryIdx bind);
  // refuse private leakage into name-recovery DFS. Residual FieldSlot
  // composites try as-is / Mode / swap only. Dual single-unit packs that
  // need E3 must either commit E3 or succeed Mode retry — never sequential
  // E2 split.
  bool SawPrivate = false;
  bool SawResidual = false;
  bool SawBare = false;
  unsigned RealKids = 0;
  for (unsigned I = 0, E = In.getNumOperands(); I != E; ++I) {
    const MCOperand &Op = In.getOperand(I);
    if (!Op.isInst() || !Op.getInst())
      continue;
    const unsigned ChildOpc = Op.getInst()->getOpcode();
    if (isFormatENopOpcode(ChildOpc, MII))
      continue;
    ++RealKids;
    if (findFormatEMemberByOpcode(ChildOpc) != nullptr) {
      SawPrivate = true;
      continue;
    }
    if (isResidualSlotMemberOpcode(ChildOpc, MII)) {
      SawResidual = true;
      continue;
    }
    SawBare = true;
  }
  if (SawPrivate)
    return false; // never DFS a composite that carries typed private members

  // Residual FieldSlots never pin entries. Try as-is / Mode / one swap
  // first; if that fails (or the pack mixes residual + bare logicals),
  // fall through to logical-name assign under the composite row. E2 vs
  // E3 is already a bundle-level fact (child count / InstSlot). Never
  // sequential E2 split.
  if (SawResidual && !SawBare) {
    const bool PrefersE3 = In.getOpcode() == Haydn::BUNDLE_E96_THREE_ENTRY;
    const bool PrefersE2 = In.getOpcode() == Haydn::BUNDLE_E96_TWO_ENTRY;
    if (PrefersE2 && RealKids <= 3) {
      if (trySerializeFormatECompositeAsIs(In, MII, MRI, Out, Storage,
                                           /*ForceMode=*/1))
        return true;
    }
    if (PrefersE3 && RealKids <= 2) {
      if (trySerializeFormatECompositeAsIs(In, MII, MRI, Out, Storage,
                                           /*ForceMode=*/0))
        return true;
    }
    if (RealKids == 2) {
      SmallVector<const MCInst *, 3> Kids;
      for (unsigned I = 0, E = In.getNumOperands(); I != E; ++I) {
        const MCOperand &Op = In.getOperand(I);
        if (!Op.isInst() || !Op.getInst())
          continue;
        if (isFormatENopOpcode(Op.getInst()->getOpcode(), MII))
          continue;
        Kids.push_back(Op.getInst());
      }
      if (Kids.size() == 2) {
        MCInst Swapped;
        Swapped.setOpcode(In.getOpcode());
        Swapped.addOperand(MCOperand::createInst(Kids[1]));
        Swapped.addOperand(MCOperand::createInst(Kids[0]));
        if (trySerializeFormatECompositeAsIs(Swapped, MII, MRI, Out, Storage))
          return true;
        if (PrefersE2) {
          if (trySerializeFormatECompositeAsIs(Swapped, MII, MRI, Out, Storage,
                                               /*ForceMode=*/1))
            return true;
        }
        if (PrefersE3) {
          if (trySerializeFormatECompositeAsIs(Swapped, MII, MRI, Out, Storage,
                                               /*ForceMode=*/0))
            return true;
        }
      }
    }
  }

  SmallVector<const MCInst *, 3> Reals;
  SmallVector<std::string, 3> LogicalNames;
  for (unsigned I = 0, E = In.getNumOperands(); I != E; ++I) {
    const MCOperand &Op = In.getOperand(I);
    if (!Op.isInst() || !Op.getInst())
      continue;
    // Residual slot NOPs may be NOP_S* / table nops — normalize via catalog name.
    std::string Log =
        formatELogicalName(MII.getName(Op.getInst()->getOpcode()));
    if (Log.empty() || StringRef(Log).equals_insensitive("NOP"))
      continue;
    Reals.push_back(Op.getInst());
    LogicalNames.push_back(std::move(Log));
  }
  if (Reals.empty() || Reals.size() > 3)
    return false;

  // Standalone hand-asm only. Prefer the composite's row; may try the other
  // one-parcel row after preferred fails — still never multi-parcel.
  const bool PrefersE3 = In.getOpcode() == Haydn::BUNDLE_E96_THREE_ENTRY;
  const bool PrefersE2 = In.getOpcode() == Haydn::BUNDLE_E96_TWO_ENTRY;
  std::optional<SmallVector<FormatEEntryAssign, 3>> Best;
  uint8_t BestMode = 0;
  auto tryMode = [&](uint8_t Mode) -> bool {
    if (LogicalNames.size() > (Mode ? 3u : 2u))
      return false;
    Best = assignFormatEMemberEntries(LogicalNames, Mode);
    if (!Best)
      return false;
    BestMode = Mode;
    return true;
  };
  if (PrefersE3) {
    if (!tryMode(/*Mode=*/1) && Reals.size() <= 2)
      tryMode(/*Mode=*/0);
  } else if (PrefersE2) {
    if (!tryMode(/*Mode=*/0) && Reals.size() <= 3)
      tryMode(/*Mode=*/1);
  } else {
    if (Reals.size() <= 2)
      tryMode(/*Mode=*/0);
    if (!Best && Reals.size() <= 3)
      tryMode(/*Mode=*/1);
  }
  if (!Best)
    return false;

  const unsigned EntryCount = BestMode ? 3u : 2u;
  Out.clear();
  Out.setOpcode(BestMode ? Haydn::BUNDLE_E96_THREE_ENTRY
                         : Haydn::BUNDLE_E96_TWO_ENTRY);

  SmallVector<int, 3> KidAtEntry(EntryCount, -1);
  for (unsigned K = 0, KE = Reals.size(); K != KE; ++K)
    KidAtEntry[(*Best)[K].EntryIdx] = static_cast<int>(K);

  Storage.clear();
  Storage.reserve(EntryCount);
  for (unsigned E = 0; E < EntryCount; ++E) {
    if (KidAtEntry[E] >= 0) {
      const int Kid = KidAtEntry[E];
      if (!(*Best)[Kid].Mem)
        return false;
      Storage.emplace_back();
      if (!fillFormatEMemberInst(*(*Best)[Kid].Mem, *Reals[Kid], MII, MRI,
                                 Storage.back()))
        return false;
      Out.addOperand(MCOperand::createInst(&Storage.back()));
    } else {
      Storage.emplace_back();
      Storage.back().setOpcode(Haydn::NOP);
      Out.addOperand(MCOperand::createInst(&Storage.back()));
    }
  }
  return true;
}

// Strip residual slot member / wide / LS suffixes to recover the logical
// catalog name used by Format E records. Shared peel lives in
// HaydnFormatERecords.h (Bundle canAdd / solver unit cover use the same map).
static std::string formatELogicalName(StringRef Name) {
  return haydn::format_e::peelLogicalOpcodeName(Name);
}

// Entry field packing is TableGen Inst{} on live Format E members
// (HaydnFormatsE96Members.td.inc). encodeSlotSubInst fills a member MCInst
// and calls getBinaryCodeForInstr — do not reintroduce hand field packers.

} // end anonymous namespace (Format E placement helpers)

/// Map logical / residual `_S*` MC operands onto a live Format E member Inst
/// (wire field order + reg classes from tblgen Desc). Placement only —
/// encodeSlotSubInst never calls this.
static bool fillFormatEMemberInst(const FormatEMemberRec &Mem,
                                  const MCInst &Logical, const MCInstrInfo &MII,
                                  const MCRegisterInfo &MRI, MCInst &Out) {
  if (Mem.MemberId >= FormatEMemberOpcodeCount)
    return false;
  const unsigned MemberOpc = FormatEMemberOpcodes[Mem.MemberId];
  if (MemberOpc == 0)
    return false;

  // Typed as-is path: SubInst is already the private Format E member opcode
  // with wire-shaped operands — copy Desc operands without bag-sort rebuild.
  if (Logical.getOpcode() == MemberOpc) {
    const MCInstrDesc &Desc = MII.get(MemberOpc);
    if (Logical.getNumOperands() < Desc.getNumOperands())
      return false;
    Out.clear();
    Out.setOpcode(MemberOpc);
    for (unsigned OI = 0, OE = Desc.getNumOperands(); OI != OE; ++OI)
      Out.addOperand(Logical.getOperand(OI));
    return true;
  }

  // Residual positional promote: when residual/slot-member operands already
  // match the private member Desc in count, order, and operand kind, copy
  // without bag-sort. Shape-mismatched residual (writeback extras, dual-dest
  // collapse, UA stream reorder) falls through to bag-sort recovery.
  {
    const MCInstrDesc &Desc = MII.get(MemberOpc);
    const unsigned Need = Desc.getNumOperands();
    if (Logical.getNumOperands() == Need && Need > 0) {
      bool PosOk = true;
      for (unsigned OI = 0; OI != Need; ++OI) {
        const MCOperand &MO = Logical.getOperand(OI);
        const MCOperandInfo &Info = Desc.operands()[OI];
        const bool WantReg = Info.OperandType == MCOI::OPERAND_REGISTER ||
                             Info.RegClass >= 0;
        if (WantReg) {
          if (!MO.isReg()) {
            PosOk = false;
            break;
          }
          if (Info.RegClass >= 0 && MO.getReg() != Haydn::NoRegister &&
              !MRI.getRegClass(Info.RegClass).contains(MO.getReg())) {
            PosOk = false;
            break;
          }
        } else if (!MO.isImm() && !MO.isExpr()) {
          PosOk = false;
          break;
        }
      }
      if (PosOk) {
        Out.clear();
        Out.setOpcode(MemberOpc);
        for (unsigned OI = 0; OI != Need; ++OI)
          Out.addOperand(Logical.getOperand(OI));
        return true;
      }
    }
  }

  // SET_HWLOOP: preserve operand order (sel, off1, off2, cnt|rs). This is
  // identity fill, not bag-sort — residual reloc SET_HWLOOP*_W already
  // cutovers; peel-identity SET_HWLOOP stays verifier-banned.
  {
    StringRef Log = Mem.Logical ? Mem.Logical : "";
    if (Log.equals_insensitive("SET_HWLOOP") ||
        Log.equals_insensitive("SET_HWLOOP_F2") ||
        Log.equals_insensitive("SET_HWLOOP_W") ||
        Log.equals_insensitive("SET_HWLOOP_F2_W")) {
      const MCInstrDesc &Desc = MII.get(MemberOpc);
      if (Logical.getNumOperands() < Desc.getNumOperands())
        return false;
      Out.clear();
      Out.setOpcode(MemberOpc);
      for (unsigned OI = 0, OE = Desc.getNumOperands(); OI != OE; ++OI)
        Out.addOperand(Logical.getOperand(OI));
      return true;
    }
  }

  SmallVector<MCRegister, 4> DRs, GPRs, ARs;
  SmallVector<int64_t, 4> Imms;
  SmallVector<const MCExpr *, 2> Exprs;
  for (unsigned I = 0, E = Logical.getNumOperands(); I != E; ++I) {
    const MCOperand &MO = Logical.getOperand(I);
    if (MO.isReg()) {
      MCRegister R = MO.getReg();
      if (R == Haydn::NoRegister)
        continue;
      if (MRI.getRegClass(Haydn::DR64RegClassID).contains(R))
        DRs.push_back(R);
      else if (MRI.getRegClass(Haydn::ARRegClassID).contains(R))
        ARs.push_back(R);
      else
        GPRs.push_back(R);
    } else if (MO.isImm()) {
      Imms.push_back(MO.getImm());
    } else if (MO.isExpr()) {
      Exprs.push_back(MO.getExpr());
    }
  }

  // LUI: vestigial middle $rs must not steal the only GPR field; keep first
  // GPR (rt) and last imm (HI12).
  StringRef Log = Mem.Logical ? Mem.Logical : "";
  if (Log.equals_insensitive("LUI")) {
    if (GPRs.size() > 1)
      GPRs.resize(1);
    if (Imms.size() > 1) {
      int64_t Last = Imms.back();
      Imms.clear();
      Imms.push_back(Last);
    }
  }
  // PLDWWUA logical: (rs, ar_sel). Format E member: (ar_sel, rs/dest2).
  if (Log.equals_insensitive("PLDWWUA") ||
      Log.equals_insensitive("PLDWWUA_POST")) {
    // Imm before GPR in member dag — collection order is already fine if
    // we only have 1 each; ensure imm is ar_sel (last if multiple).
    if (Imms.size() > 1) {
      int64_t Last = Imms.back();
      Imms.clear();
      Imms.push_back(Last);
    }
  }

  const MCInstrDesc &Desc = MII.get(MemberOpc);
  unsigned NeedDR = 0, NeedGPR = 0, NeedAR = 0, NeedImm = 0;
  for (unsigned OI = 0, OE = Desc.getNumOperands(); OI != OE; ++OI) {
    const MCOperandInfo &OIInfo = Desc.operands()[OI];
    const bool IsReg = OIInfo.OperandType == MCOI::OPERAND_REGISTER ||
                       OIInfo.RegClass >= 0;
    if (IsReg) {
      if (OIInfo.RegClass == (int)Haydn::DR64RegClassID)
        ++NeedDR;
      else if (OIInfo.RegClass == (int)Haydn::ARRegClassID)
        ++NeedAR;
      else
        ++NeedGPR;
    } else {
      ++NeedImm;
    }
  }

  // PRE/POST AGU: logical MC is [dst, rs_wb, rs, imm] with $rs=$rs_wb, so
  // GPRs=[dst, wb, base] and wb==base. Wire member wants [dst, base, imm].
  // Drop the middle tied writeback — do NOT skip the front (that drops dst
  // and was encoding s_lw_pre_imm fp,r4,imm as r4,r4,imm).
  if (GPRs.size() == NeedGPR + 1 && GPRs.size() >= 3 && GPRs[1] == GPRs[2]) {
    SmallVector<MCRegister, 4> Fixed;
    Fixed.push_back(GPRs[0]);
    for (unsigned I = 2, E = GPRs.size(); I != E; ++I)
      Fixed.push_back(GPRs[I]);
    GPRs = std::move(Fixed);
  }
  // Same pattern for DR dest + GPR base writeback (D_*_PRE/POST_IMM).
  if (DRs.size() == NeedDR && GPRs.size() == NeedGPR + 1 && GPRs.size() >= 2 &&
      NeedGPR >= 1 && GPRs[0] == GPRs[1]) {
    // [rs_wb, rs, …] with tie, no separate dst in GPR list (dst is DR).
    SmallVector<MCRegister, 4> Fixed;
    Fixed.push_back(GPRs[0]); // keep one of the tied pair
    for (unsigned I = 2, E = GPRs.size(); I != E; ++I)
      Fixed.push_back(GPRs[I]);
    GPRs = std::move(Fixed);
  }
  // DR RMW (X2MOVT32 / X2MOVF32 / X4MOVT16 / …): logical is
  // [rd, rs_false, rs_true] with rd==rs_false after selector seed-copy.
  // Format E wire is 2-op (rtd, rsd). Collapse the tied leading pair so
  // DrSkip does not drop rd and keep (false,true) with the wrong dest.
  if (DRs.size() == NeedDR + 1 && DRs.size() >= 2 && DRs[0] == DRs[1]) {
    SmallVector<MCRegister, 4> Fixed;
    Fixed.push_back(DRs[0]);
    for (unsigned I = 2, E = DRs.size(); I != E; ++I)
      Fixed.push_back(DRs[I]);
    DRs = std::move(Fixed);
  }

  // Dual-dest 4-DR logical (rd, rtd2, rs1, rs2) matches Format E MAC RRR
  // member dag order (dest1, dest2, src1, src2) — identity fill.
  // Compressed 3-field (src1 empty): dest1, src2, dest2 ← rd, rs2, rtd2.
  // Blind front-skip used to drop the first dest and scramble dual-src MAC.
  if (DRs.size() == 4 && NeedDR == 3) {
    DRs = {DRs[0], DRs[3], DRs[1]};
  }

  // Format E UA stream / AR residual: wire is [ar_sel, (rtd), rs_base].
  // Logical MC is [rtd?, rs1_wb, rs1, rs2, ar_sel, dir_sel] with rs1=rs1_wb.
  // After the tied-pair collapse above GPRs=[rs1, rs2] and Imms=[ar_sel,dir].
  // Generic front-skip would keep stride/dir (last) — load EA becomes stride
  // (e.g. 8 → fault at 0x10). Keep the leading base + ar_sel instead.
  const bool IsUaOrArStream =
      Log.contains_insensitive("UA_POST") ||
      Log.equals_insensitive("WBARWUA") ||
      Log.equals_insensitive("PLDWWUA") ||
      Log.equals_insensitive("PLDWWUA_POST") ||
      Log.equals_insensitive("FLAR");
  if (IsUaOrArStream) {
    if (GPRs.size() > NeedGPR)
      GPRs.resize(NeedGPR);
    if (Imms.size() > NeedImm)
      Imms.resize(NeedImm);
  }

  // Extra GPRs at front are typically dead rd (CSRW outs rd unused on wire).
  const unsigned GprSkip =
      GPRs.size() > NeedGPR ? GPRs.size() - NeedGPR : 0;
  const unsigned DrSkip = DRs.size() > NeedDR ? DRs.size() - NeedDR : 0;
  const unsigned ArSkip = ARs.size() > NeedAR ? ARs.size() - NeedAR : 0;
  // Prefer concrete imms first; remaining imm slots take MCExprs (branches).
  const unsigned ImmAvail = Imms.size() + Exprs.size();
  const unsigned ImmSkip =
      ImmAvail > NeedImm ? ImmAvail - NeedImm : 0;

  unsigned Di = DrSkip, Gi = GprSkip, Ai = ArSkip;
  unsigned Ii = 0, Ei = 0;
  // Skip extras from front of the combined imm/expr stream.
  unsigned ToSkip = ImmSkip;
  while (ToSkip > 0) {
    if (Ii < Imms.size()) {
      ++Ii;
      --ToSkip;
    } else if (Ei < Exprs.size()) {
      ++Ei;
      --ToSkip;
    } else {
      break;
    }
  }
  Out.clear();
  Out.setOpcode(MemberOpc);
  for (unsigned OI = 0, OE = Desc.getNumOperands(); OI != OE; ++OI) {
    const MCOperandInfo &OIInfo = Desc.operands()[OI];
    const bool IsReg = OIInfo.OperandType == MCOI::OPERAND_REGISTER ||
                       OIInfo.RegClass >= 0;
    if (IsReg) {
      MCRegister R = Haydn::R0;
      if (OIInfo.RegClass == (int)Haydn::DR64RegClassID) {
        if (Di >= DRs.size())
          return false;
        R = DRs[Di++];
      } else if (OIInfo.RegClass == (int)Haydn::ARRegClassID) {
        if (Ai >= ARs.size())
          return false;
        R = ARs[Ai++];
      } else {
        if (Gi >= GPRs.size())
          return false;
        R = GPRs[Gi++];
      }
      Out.addOperand(MCOperand::createReg(R));
    } else {
      if (Ii < Imms.size())
        Out.addOperand(MCOperand::createImm(Imms[Ii++]));
      else if (Ei < Exprs.size())
        Out.addOperand(MCOperand::createExpr(Exprs[Ei++]));
      else
        return false;
    }
  }
  return true;
}

void HaydnMCCodeEmitter::encodeSlotSubInst(
    const MCInst &Composite, const MCInst &SubInst, APInt &Op,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
  // Format E product composites only (FE8): golden entry pack into E2/E3
  // entry widths (E2: 45/41, E3: 31/31/27). Non-Format-E composites fatal.
  unsigned SlotIdx = 0;
  for (unsigned I = 0, E = Composite.getNumOperands(); I != E; ++I) {
    const MCOperand &MO = Composite.getOperand(I);
    if (MO.isInst() && MO.getInst() == &SubInst) {
      SlotIdx = I;
      break;
    }
  }

  const bool IsFormatE2 = Composite.getOpcode() == Haydn::BUNDLE_E96_TWO_ENTRY;
  const bool IsFormatE3 =
      Composite.getOpcode() == Haydn::BUNDLE_E96_THREE_ENTRY;

  MCSlotKind Kind;
  unsigned EntryWidth = 0;
  unsigned EntryLSB = 0; // LSB bit position of entry field in 96-bit parcel
  if (IsFormatE2) {
    // Generated encoder: e0 @ bits[50:6] (45b), e1 @ bits[91:51] (41b).
    switch (SlotIdx) {
    default:
      llvm_unreachable("E2 entry index must be 0 or 1");
    case 0:
      Kind = MCSlotKind::Haydn_SLOT_E2_0;
      EntryWidth = 45;
      EntryLSB = 6;
      break;
    case 1:
      Kind = MCSlotKind::Haydn_SLOT_E2_1;
      EntryWidth = 41;
      EntryLSB = 51;
      break;
    }
  } else if (IsFormatE3) {
    // Generated encoder: e0 @ [36:6] (31b), e1 @ [67:37] (31b), e2 @ [94:68] (27b).
    switch (SlotIdx) {
    default:
      llvm_unreachable("E3 entry index must be 0, 1, or 2");
    case 0:
      Kind = MCSlotKind::Haydn_SLOT_E3_0;
      EntryWidth = 31;
      EntryLSB = 6;
      break;
    case 1:
      Kind = MCSlotKind::Haydn_SLOT_E3_1;
      EntryWidth = 31;
      EntryLSB = 37;
      break;
    case 2:
      Kind = MCSlotKind::Haydn_SLOT_E3_2;
      EntryWidth = 27;
      EntryLSB = 68;
      break;
    }
  } else {
    report_fatal_error(
        "Haydn MC: slot sub-instruction encode only accepts Format E "
        "composites (BUNDLE_E96_*) — legacy composite path retired (FE8)",
        /*GenCrashDiag=*/false);
  }

  // Product Format E: TableGen Inst{} on live members encodes the entry
  // window. C++ does not pack entry fields. Residual truncate is forbidden.
  (void)Kind;
  (void)EntryLSB;
  if (isFormatENopOpcode(SubInst.getOpcode(), MII)) {
    Op = APInt(EntryWidth, 0); // zero entry → NOP under Format E inverse
    return;
  }
  const uint8_t Mode = IsFormatE3 ? 1 : 0;

  // Claim units already chosen for lower entry indices. Previous entries
  // are generated members or NOP after placement; no name recovery.
  auto claimPrevUnits = [&]() -> uint32_t {
    uint32_t UsedUnits = 0;
    for (unsigned S = 0; S < SlotIdx && S < Composite.getNumOperands(); ++S) {
      const MCOperand &Prev = Composite.getOperand(S);
      if (!Prev.isInst() || !Prev.getInst())
        continue;
      if (isFormatENopOpcode(Prev.getInst()->getOpcode(), MII))
        continue;
      const FormatEMemberRec *PM =
          findFormatEMemberByOpcode(Prev.getInst()->getOpcode());
      if (PM && (PM->Mode != Mode || PM->EntryIdx != static_cast<uint8_t>(S)))
        PM = nullptr;
      if (PM && PM->Unit < 32)
        UsedUnits |= (1u << PM->Unit);
    }
    return UsedUnits;
  };

  // Serialize-only: SubInst must already be the generated private member
  // for this (Mode, Entry). Placement owns FieldSlot→MemberId and standalone
  // DFS fill. Name peel / fillFormatEMemberInst are not reachable here.
  const FormatEMemberRec *Typed =
      findFormatEMemberByOpcode(SubInst.getOpcode());
  if (!Typed) {
    report_fatal_error(
        Twine("Haydn MC: Format E entry encode saw non-member opcode '") +
            MII.getName(SubInst.getOpcode()) +
            "' — placement must emit generated members; refuse name "
            "recovery / operand rebuild",
        /*GenCrashDiag=*/false);
  }
  if (Typed->Mode != Mode || Typed->EntryIdx != static_cast<uint8_t>(SlotIdx)) {
    report_fatal_error(
        Twine("Haydn MC: committed Format E member '") +
            MII.getName(SubInst.getOpcode()) +
            "' does not match mode=" +
            Twine(static_cast<unsigned>(Mode)) + " entry=" +
            Twine(SlotIdx) + " — refuse logical re-place (serialize-only)",
        /*GenCrashDiag=*/false);
  }
  const uint32_t UsedUnits = claimPrevUnits();
  if (Typed->Unit < 32 && (UsedUnits & (1u << Typed->Unit))) {
    report_fatal_error(
        Twine("Haydn MC: committed Format E member '") +
            MII.getName(SubInst.getOpcode()) +
            "' collides unit with earlier entry — refuse re-place",
        /*GenCrashDiag=*/false);
  }
  if (Typed->MemberId >= FormatEMemberOpcodeCount) {
    report_fatal_error(
        Twine("Haydn MC: committed Format E MemberId out of range for '") +
            MII.getName(SubInst.getOpcode()) + "'",
        /*GenCrashDiag=*/false);
  }
  const unsigned MemberOpc = FormatEMemberOpcodes[Typed->MemberId];
  if (MemberOpc == Haydn::NOP || isFormatENopOpcode(MemberOpc, MII)) {
    Op = APInt(EntryWidth, 0);
    return;
  }
  if (SubInst.getOpcode() != MemberOpc) {
    report_fatal_error(
        Twine("Haydn MC: committed member opcode mismatch MemberId table "
              "for '") +
            MII.getName(SubInst.getOpcode()) + "'",
        /*GenCrashDiag=*/false);
  }
  SmallVector<MCFixup, 4> LocalFixups;
  APInt Scratch(96, 0);
  APInt InstBits(96, 0);
  getBinaryCodeForInstr(SubInst, LocalFixups, InstBits, Scratch, STI);
  Op = InstBits.zextOrTrunc(EntryWidth);
  for (const MCFixup &F : LocalFixups)
    addHaydnFixup(Fixups, F.getOffset(), F.getValue(), F.getKind());
}

//===----------------------------------------------------------------------===//
// Fixup helpers
//===----------------------------------------------------------------------===//

unsigned HaydnMCCodeEmitter::getBranchFixupKind(const MCInst &MI) const {
  // Same logical-name map as getExprFixupKind. Do not switch on residual
  // `_S*` FieldSlots — MemberId / peel covers generated members and keeps.
  const FormatEMemberRec *Typed = findFormatEMemberByOpcode(MI.getOpcode());
  std::string LogicalStorage;
  if (Typed && Typed->Logical && Typed->Logical[0] != '\0')
    LogicalStorage = Typed->Logical;
  if (LogicalStorage.empty())
    LogicalStorage = formatELogicalName(MII.getName(MI.getOpcode()));
  const StringRef Logical = LogicalStorage;
  if (Logical.equals_insensitive("BEQZ") ||
      Logical.equals_insensitive("BNEZ") ||
      Logical.equals_insensitive("BLTZ") ||
      Logical.equals_insensitive("BGEZ") ||
      Logical.equals_insensitive("BEQZ_W") ||
      Logical.equals_insensitive("BNEZ_W") ||
      Logical.equals_insensitive("BGEZ_W") ||
      Logical.equals_insensitive("BLTZ_W"))
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12;
  return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
}

unsigned HaydnMCCodeEmitter::getCallFixupKind(const MCInst &MI) const {
  // Format E JAL (incl. JAL_E2_… members): WIDE_CallSImm20 @ bits[31:50].
  const std::string Logical = formatELogicalName(MII.getName(MI.getOpcode()));
  if (StringRef(Logical).equals_insensitive("JAL") ||
      MI.getOpcode() == Haydn::JAL)
    return Haydn::FIXUP_HAYDN_WIDE_CallSImm20;
  if (StringRef(Logical).equals_insensitive("JALR") ||
      MI.getOpcode() == Haydn::JALR)
    return Haydn::FIXUP_HAYDN_BranchSImm16;
  return Haydn::FIXUP_HAYDN_WIDE_CallSImm20;
}

//===----------------------------------------------------------------------===//
// Operand value extraction
//===----------------------------------------------------------------------===//

void
HaydnMCCodeEmitter::getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                      APInt &Op, SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  if (MO.isReg()) {
    Op = Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());
    return;
  }
  if (MO.isImm()) {
    Op = static_cast<uint64_t>(MO.getImm());
    return;
  }
  if (MO.isExpr()) {
    // Hexagon-style kind→PCRel (addHaydnFixup). JAL_S0's generated
    // encoder routes here (getMachineOpValue), not getCallTargetOpValue.
    // Resolve operand index so SET_HWLOOP Off1/Off2 get distinct typed kinds
    // (Format E members share getMachineOpValue for both uimm fields).
    unsigned OpNo = ~0u;
    for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
      if (&MI.getOperand(I) == &MO) {
        OpNo = I;
        break;
      }
    }
    addHaydnFixup(Fixups, /*Offset=*/0, MO.getExpr(),
                  getExprFixupKind(MI, MII, OpNo, MO.getExpr()));
    Op = 0;
    return;
  }
  if (MO.isInst()) {
    // Format E composites hold entry sub-instructions as MCOperand::isInst.
    encodeSlotSubInst(MI, *MO.getInst(), Op, Fixups, STI);
    return;
  }
  llvm_unreachable("Unhandled operand type in getMachineOpValue");
}

void
HaydnMCCodeEmitter::getBranchTargetOpValue(const MCInst &MI, unsigned OpNo,
                                           APInt &Op,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  unsigned Opcode = MI.getOpcode();

  // GE96-03: branches store byte PC+imm (no ÷2). Bundle min-align is 2.
  (void)Opcode;
  unsigned Alignment = 2;
  unsigned Shift = 0;

  if (MO.isImm()) {
    int64_t Imm = MO.getImm();
    if (Imm & (Alignment - 1)) {
      Ctx.reportError(SMLoc(), "branch offset must be 2-byte aligned");
    }
    Op = static_cast<uint64_t>(Imm >> Shift);
    return;
  }

  if (MO.isExpr()) {
    unsigned Kind = getBranchFixupKind(MI);
    if (std::optional<unsigned> SpecKind =
            fixupKindFromSpecifier(MO.getExpr()))
      Kind = *SpecKind;
    addHaydnFixup(Fixups, /*Offset=*/0, MO.getExpr(), Kind);
    Op = 0;
    return;
  }

  llvm_unreachable("Unhandled operand type in getBranchTargetOpValue");
}

void
HaydnMCCodeEmitter::getCallTargetOpValue(const MCInst &MI, unsigned OpNo,
                                         APInt &Op,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  unsigned Opcode = MI.getOpcode();
  bool IsJAL = (Opcode == Haydn::JAL);

  if (MO.isImm()) {
    int64_t Imm = MO.getImm();
    if (IsJAL) {
      // GE96-03: JAL field stores byte PC+imm (no ÷2).
      Op = static_cast<uint64_t>(Imm & 0xFFFFF);
      return;
    }
    Op = static_cast<uint64_t>(Imm);
    return;
  }

  if (MO.isExpr()) {
    unsigned Kind = getCallFixupKind(MI);
    if (std::optional<unsigned> SpecKind =
            fixupKindFromSpecifier(MO.getExpr()))
      Kind = *SpecKind;
    addHaydnFixup(Fixups, /*Offset=*/0, MO.getExpr(), Kind);
    Op = 0;
    return;
  }

  if (MO.isReg()) {
    Op = Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());
    return;
  }

  llvm_unreachable("Unhandled operand type in getCallTargetOpValue");
}

//===----------------------------------------------------------------------===//
// AIE-style scaled-immediate encoder for logical *_W / *_dr / hwloop_off*
// operand classes (Format E encode).
//
// Bound to the `*_wide` / `*_dr` / `hwloop_off*` operand classes via tablegen
// `EncoderMethod`. Reproduces the bit layout per operand:
// immediate form: range-check alignment, then (Imm >> Shift) masked to N.
// symbolic form: push one MCFixup of `FixupKind` at parcel Offset=0.
// FixupKind == Haydn::FIXUP_HAYDN_NONE: pure-immediate field, skip the
// fixup push.
//===----------------------------------------------------------------------===//
template <unsigned N, unsigned Shift, bool IsSigned, bool IsPCRel,
          Haydn::Fixups FixupKind>
void HaydnMCCodeEmitter::getSImmOpValueXStepWide(
    const MCInst &MI, unsigned OpNo, APInt &Op,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
  (void)IsSigned;  // bit-identical for sign/zero at the field level
  (void)STI;

  if (OpNo >= MI.getNumOperands()) {
    Op = 0;
    return;
  }

  const MCOperand &MO = MI.getOperand(OpNo);

  if (MO.isImm()) {
    int64_t Imm = MO.getImm();
    if (Shift > 0) {
      unsigned Alignment = 1u << Shift;
      if (Imm & (Alignment - 1)) {
        Ctx.reportError(SMLoc(),
                        Twine("offset must be ") + Twine(Alignment) +
                            "-byte aligned");
      }
    }
    uint64_t Mask = (N >= 64) ? ~0ULL : ((uint64_t{1} << N) - 1);
    Op = static_cast<uint64_t>(Imm >> Shift) & Mask;
    return;
  }

  if (MO.isExpr()) {
    if (std::optional<unsigned> SpecKind =
            fixupKindFromSpecifier(MO.getExpr())) {
      addHaydnFixup(Fixups, /*Offset=*/0, MO.getExpr(), *SpecKind);
    } else if (FixupKind != Haydn::FIXUP_HAYDN_NONE) {
      Fixups.push_back(MCFixup::create(
          /*Offset=*/0, MO.getExpr(), static_cast<MCFixupKind>(FixupKind),
          IsPCRel));
    }
    Op = 0;
    return;
  }

  Op = 0;
}

// Explicit instantiation is unnecessary: the tablegen-generated
// HaydnGenMCCodeEmitter.inc instantiates this template for every operand
// class that references it via EncoderMethod.

// Residual path: getSImmOpValueXStepWide + getMachineOpValue via generated
// formats. Multi-width Mode-0 / page-1 EncoderMethods are deleted.

MCCodeEmitter *llvm::createHaydnMCCodeEmitter(const MCInstrInfo &MCII,
                                               MCContext &Ctx) {
  return new HaydnMCCodeEmitter(Ctx, MCII);
}

//===----------------------------------------------------------------------===//
// Long branch pseudo expansion
//
// Expands a long-branch pseudo to: inverted-conditional-branch + JAL. Each
// emitted real instruction recurses through `encodeInstruction` (Format E
// product parcels). The inverted branch carries a literal skip past this
// parcel + the following JAL; the JAL's symbolic target fixup is adjusted by
// one production EncodedBytes (registry parcel size).
//
// Control immediates in MCInst are **byte** PC deltas (same as fixup Values
// and getBranchTargetOpValue). GE96-03: field stores bytes (ValueShift=0).
// A 2-parcel skip is createImm(2 * Parcel) bytes.
//===----------------------------------------------------------------------===//

// Map a long branch pseudo to the inverted conditional branch opcode.
static unsigned getInvertedBranchOpcode(unsigned LongBrOpc) {
  switch (LongBrOpc) {
  default:
    llvm_unreachable("Unexpected long branch opcode!");
  case Haydn::PseudoLongBEQ:  return Haydn::BNE;
  case Haydn::PseudoLongBNE:  return Haydn::BEQ;
  case Haydn::PseudoLongBGE:  return Haydn::BLT;
  case Haydn::PseudoLongBGEU: return Haydn::BLTU;
  case Haydn::PseudoLongBLT:  return Haydn::BGE;
  case Haydn::PseudoLongBLTU: return Haydn::BGEU;
  case Haydn::PseudoLongBEQZ: return Haydn::BNEZ;
  case Haydn::PseudoLongBNEZ: return Haydn::BEQZ;
  case Haydn::PseudoLongBGEZ: return Haydn::BLTZ;
  case Haydn::PseudoLongBLTZ: return Haydn::BGEZ;
  // Unconditional: no inverted condition needed; just JAL.
  case Haydn::PseudoLongB:    return 0;
  }
}

void HaydnMCCodeEmitter::expandLongBranch(
    const MCInst &MI, SmallVectorImpl<char> &CB,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
  unsigned LongOpc = MI.getOpcode();
  unsigned InvOpc = getInvertedBranchOpcode(LongOpc);
  const unsigned Parcel = haydnProductionParcelBytes().Value;
  // Byte skip over this inverted-branch parcel + the following JAL parcel.
  // GE96-03: field stores bytes (ValueShift=0).
  const int64_t SkipBytes = static_cast<int64_t>(2u * Parcel);

  // Record the fixup count before the inverted branch so we can drop any
  // spurious fixup it produces (literal skip, not a symbol).
  const size_t FixupBeforeInv = Fixups.size();

  if (InvOpc != 0) {
    // Inverted conditional: when taken, skip this Format E parcel + the JAL.
    MCInst InvBr;
    InvBr.setOpcode(InvOpc);

    if (LongOpc == Haydn::PseudoLongBEQ || LongOpc == Haydn::PseudoLongBNE ||
        LongOpc == Haydn::PseudoLongBGE || LongOpc == Haydn::PseudoLongBGEU ||
        LongOpc == Haydn::PseudoLongBLT || LongOpc == Haydn::PseudoLongBLTU) {
      // Two-register form: rs1, rs2, skip_offset
      InvBr.addOperand(MI.getOperand(0)); // rs1
      InvBr.addOperand(MI.getOperand(1)); // rs2
      InvBr.addOperand(MCOperand::createImm(SkipBytes));
    } else {
      // Single-register form: rs, skip_offset
      InvBr.addOperand(MI.getOperand(0)); // rs
      InvBr.addOperand(MCOperand::createImm(SkipBytes));
    }

    // Emit as one Format E product parcel. Literal skip — drop any fixup.
    encodeInstruction(InvBr, CB, Fixups, STI);
    Fixups.resize(FixupBeforeInv);
  }
  // PseudoLongB (unconditional): no inverted branch; the JAL alone suffices.

  // Emit the JAL that jumps to the actual target.
  // JAL R0, target — R0 as destination discards the return address.
  MCInst Jal;
  Jal.setOpcode(Haydn::JAL);
  Jal.addOperand(MCOperand::createReg(Haydn::R0));

  // The target operand is the last operand of the long branch pseudo.
  unsigned TargetOpIdx;
  if (LongOpc == Haydn::PseudoLongBEQ || LongOpc == Haydn::PseudoLongBNE ||
      LongOpc == Haydn::PseudoLongBGE || LongOpc == Haydn::PseudoLongBGEU ||
      LongOpc == Haydn::PseudoLongBLT || LongOpc == Haydn::PseudoLongBLTU) {
    TargetOpIdx = 2;
  } else if (LongOpc == Haydn::PseudoLongB) {
    TargetOpIdx = 0;
  } else {
    TargetOpIdx = 1;
  }

  Jal.addOperand(MI.getOperand(TargetOpIdx));

  // Emit JAL as Format E. Adjust fixup offsets by the preceding inverted
  // branch parcel (production EncodedBytes) when present.
  const size_t FixupBeforeJal = Fixups.size();
  encodeInstruction(Jal, CB, Fixups, STI);
  if (InvOpc != 0) {
    for (size_t I = FixupBeforeJal; I < Fixups.size(); ++I) {
      MCFixup &F = Fixups[I];
      Fixups[I] =
          MCFixup::create(F.getOffset() + Parcel, F.getValue(), F.getKind(),
                          F.isPCRel());
    }
  }
}

#include "HaydnGenMCCodeEmitter.inc"
