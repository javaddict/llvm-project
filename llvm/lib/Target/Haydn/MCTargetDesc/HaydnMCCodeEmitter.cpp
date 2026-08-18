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
//   standalone real opcode    -> wrap as Format E E2 singleton (+ NOP underfill)
// Long-branch is CodeGen BranchRelaxation only — no PseudoLongB* / expandLongBranch.
// Placement failure fails closed — no sequential E2 singleton split (layout-size
// fiction vs BranchRelaxation / FixupHwLoops). Residual FieldSlot composites
// serialize as-is only (no ForceMode / swap retry); they never free-DFS. Bounded
// one-parcel DFS is standalone hand-asm only (golden logical names). Compiler
// 3-child rebind (store at E3 e0 LOADSTORE0; dual loads LS0+LOAD1) lives in
// Finalize assignFormatEMemberEntries. FieldSlot suffixes never pin entries.
// encodeSlotSubInst is serialize-only: every non-NOP child must already be a
// generated private member. Placement fill is as-is / positional / SET_HWLOOP
// identity only — class-bag operand rebuild is deleted.
//
//===----------------------------------------------------------------------===//

#include "HaydnMCCodeEmitter.h"
#include "HaydnFormatERecords.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFixupKinds.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "MCTargetDesc/HaydnRelocLayout.h"
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
#include "llvm/Support/ErrorHandling.h"
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
  // Single source: RelocFieldInfo.IsPCRel. JALR resolves through the
  // dedicated JALRSImm12 row (W27: never the RI12 branch row).
  const HaydnReloc::RelocKind R = HaydnReloc::mapFixupKind(Kind);
  if (R == HaydnReloc::RelocKind::Invalid)
    return false;
  return HaydnReloc::getRelocFieldInfo(R).IsPCRel;
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

/// Resolve the generated Format E type record for \p MI. Prefer the committed
/// MemberId; otherwise the first catalog row whose Logical matches the peeled
/// name. No mnemonic→kind switch — TypeName + TypeOpcode are the schema key.
static const FormatEMemberRec *
resolveFormatETypeMember(const MCInst &MI, const MCInstrInfo &MII) {
  if (const FormatEMemberRec *Typed = findFormatEMemberByOpcode(MI.getOpcode()))
    return Typed;
  const std::string Logical = formatELogicalName(MII.getName(MI.getOpcode()));
  if (Logical.empty())
    return nullptr;
  for (unsigned I = 0; I < FormatEMemberCount; ++I) {
    const FormatEMemberRec &M = FormatEMembers[I];
    if (M.IsNop || !M.Logical || !M.TypeName)
      continue;
    if (StringRef(M.Logical).equals_insensitive(Logical))
      return &M;
  }
  return nullptr;
}

// Determine the appropriate fixup kind for an expression operand.
// MCSpecifierExpr (%hi12/%lo20/%pc_lo20) selects the kind (R12). Bare
// symbols use AIE findFixupfromFixupFields against HaydnRelocLayout —
// generated TypeName + type-opcode + field size, not a logical-name
// switch. Unknown types return nullopt. Callers refuse FIXUP_HAYDN_32
// (F19). JALR (RI12 opc 1) resolves to the dedicated JALRSImm12 row
// (W27: never borrows the branch row; GE96-03 stays rs+imm12).
// OpNo selects HWLoop Off1 (6-bit) vs Off2 (12-bit); unknown → Off1
// (W37/W38 keep).
static std::optional<unsigned> getExprFixupKind(const MCInst &MI,
                                                const MCInstrInfo &MII,
                                                unsigned OpNo = ~0u,
                                                const MCExpr *Expr = nullptr) {
  if (std::optional<unsigned> SpecKind = fixupKindFromSpecifier(Expr))
    return *SpecKind;

  const FormatEMemberRec *Mem = resolveFormatETypeMember(MI, MII);
  if (!Mem || !Mem->TypeName)
    return std::nullopt;

  unsigned FieldSize = 0;
  const StringRef TypeName = Mem->TypeName;
  if (TypeName == "HWLRIIR" || TypeName == "HWLRIII")
    FieldSize = (OpNo == 2) ? 12u : 6u;

  const HaydnReloc::FixupField Field{HaydnReloc::kUnspecifiedFieldLsb,
                                     FieldSize};
  const bool IsLS =
      Mem->Unit == static_cast<uint8_t>(FormatEUnit::LOADSTORE0) ||
      Mem->Unit == static_cast<uint8_t>(FormatEUnit::LOAD1);
  const HaydnReloc::RelocKind R = HaydnReloc::findFixupFromFixupFields(
      TypeName, Mem->Opcode, Field, /*FormatBytes=*/12, IsLS);
  if (R == HaydnReloc::RelocKind::Invalid)
    return std::nullopt;
  const unsigned FK = HaydnReloc::mapRelocKindToFixup(R);
  if (FK == Haydn::FIXUP_HAYDN_INVALID)
    return std::nullopt;
  return FK;
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
      APInt Word = InstBits.zextOrTrunc(ProdBits.Value);
      if ((Word.extractBitsAsZExtValue(3, 0) & 0x7u) !=
          haydn::format::FormatEIndicatorBits) {
        report_fatal_error(
            "Haydn MC: Format E parcel missing indicator 111 after encode",
            /*GenCrashDiag=*/false);
      }
      const uint32_t Base = static_cast<uint32_t>(CB.size());
      // AIE translateFixupsInComposite (AIEBaseMCCodeEmitter.cpp:231-232)
      // emits the composite MCFixup at offset 0 after field-identity
      // translation. Haydn FieldLsb is absolute parcel bits with r_offset =
      // parcel origin (HaydnRelocLayout). Member encoders may still report a
      // mid-parcel field byte (E3 I12 sits at bit 23 → byte 2); keep the
      // reloc at the parcel base so linked B/JAL targets stay exact records.
      const unsigned Parcel = haydnProductionParcelBytes().Value;
      for (const MCFixup &F : LocalFixups) {
        const uint32_t Abs = Base + F.getOffset();
        const uint32_t ParcelBase = Abs - (Abs % Parcel);
        addHaydnFixup(Fixups, ParcelBase, F.getValue(), F.getKind());
      }
      haydnEmitFormatEParcelLE(Word, CB);
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
    // Residual FieldSlot composites never free-DFS or ForceMode-retry;
    // reaching here means as-is serialize failed entirely.
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
///    residual operand shape matches (positional copy only; no bag-sort).
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
    // shape-mismatched residual fails closed (no bag-sort).
    // encodeSlotSubInst never peels or rebuilds.
    Storage.emplace_back();
    if (!fillFormatEMemberInst(*MemAt[E], *ChildAt[E], MII, MRI,
                               Storage.back()))
      return false;
    Out.addOperand(MCOperand::createInst(&Storage.back()));
  }
  return true;
}

/// Place real children onto Format E entries with unit injectivity.
/// As-is first. Residual FieldSlot never ForceMode/swap-retries and never
/// free-DFS (Finalize assignFormatEMemberEntries owns 3-child rebind).
/// Typed private members bind by committed EntryIdx, never DFS. Bare
/// hand-asm is the separately typed standalone entry and may one-parcel DFS.
static bool buildFormatEPlacedComposite(const MCInst &In,
                                        const MCInstrInfo &MII,
                                        const MCRegisterInfo &MRI, MCInst &Out,
                                        SmallVectorImpl<MCInst> &Storage) {
  if (trySerializeFormatECompositeAsIs(In, MII, MRI, Out, Storage))
    return true;

  // Typed private Format E members must serialize as-is (EntryIdx bind);
  // refuse private leakage into name-recovery DFS. Residual FieldSlot
  // composites serialize as-is only — no ForceMode / swap retry. Dual
  // single-unit packs that need E3 must commit E3; MC never restamps the
  // row. Bare hand-asm may one-parcel DFS below.
  bool SawPrivate = false;
  bool SawResidual = false;
  bool SawBare = false;
  for (unsigned I = 0, E = In.getNumOperands(); I != E; ++I) {
    const MCOperand &Op = In.getOperand(I);
    if (!Op.isInst() || !Op.getInst())
      continue;
    const unsigned ChildOpc = Op.getInst()->getOpcode();
    if (isFormatENopOpcode(ChildOpc, MII))
      continue;
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

  // Residual FieldSlot composites serialize as-is only. ForceMode E2↔E3
  // and dual-swap retry rewrote a committed row after verify — refuse
  // that silent repair. encodeInstruction fatals on false (serialize-only).
  // Bare logicals (hand-asm) may still one-parcel DFS below.
  if (SawResidual && !SawBare)
    return false;

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
  // without bag-sort. Shape-mismatched residual fails closed.
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

  // Closed keep-map (same law as Finalize fieldSlotKeepOperands):
  // identity, tied-acc drop, trailing extra uses, dest-as-ins, CB
  // writeback, AR-UA POST (rs2/dir_sel unencoded). Not a class bag-sort.
  {
    const MCInstrDesc &OldDesc = MII.get(Logical.getOpcode());
    const MCInstrDesc &NewDesc = MII.get(MemberOpc);
    const unsigned OldN = OldDesc.getNumOperands();
    const unsigned NewN = NewDesc.getNumOperands();
    const unsigned Have = Logical.getNumOperands();
    auto kindOk = [&](unsigned OldI, unsigned NewI) -> bool {
      if (OldI >= Have)
        return false;
      const MCOperand &MO = Logical.getOperand(OldI);
      const MCOperandInfo &Info = NewDesc.operands()[NewI];
      const bool WantReg = Info.OperandType == MCOI::OPERAND_REGISTER ||
                           Info.RegClass >= 0;
      if (WantReg) {
        if (!MO.isReg())
          return false;
        if (Info.RegClass >= 0 && MO.getReg() != Haydn::NoRegister &&
            !MRI.getRegClass(Info.RegClass).contains(MO.getReg()))
          return false;
        return true;
      }
      return MO.isImm() || MO.isExpr();
    };
    auto emitKeep = [&](ArrayRef<unsigned> Keep) -> bool {
      if (Keep.size() != NewN)
        return false;
      for (unsigned NewI = 0; NewI != NewN; ++NewI)
        if (!kindOk(Keep[NewI], NewI))
          return false;
      Out.clear();
      Out.setOpcode(MemberOpc);
      for (unsigned NewI = 0; NewI != NewN; ++NewI)
        Out.addOperand(Logical.getOperand(Keep[NewI]));
      return true;
    };
    if (OldN == 0 && NewN == 0 && Have == 0) {
      Out.clear();
      Out.setOpcode(MemberOpc);
      return true;
    }
    if (OldN > 0 && NewN > 0 && Have >= OldN) {
      // AsmString may omit a logical ins register (LUI $rs, CSRR $rs).
      // The parser then defaults that slot to Imm 0. Drop those holes
      // before trailing-use so the real imm/expr is kept.
      if (OldDesc.getNumDefs() == NewDesc.getNumDefs() && OldN > NewN) {
        SmallVector<unsigned, 4> Keep;
        bool DroppedHole = false;
        for (unsigned I = 0; I != OldN; ++I) {
          const MCOperandInfo &OldInfo = OldDesc.operands()[I];
          const bool OldWantsReg =
              OldInfo.OperandType == MCOI::OPERAND_REGISTER ||
              OldInfo.RegClass >= 0;
          if (OldWantsReg && I < Have && !Logical.getOperand(I).isReg()) {
            DroppedHole = true;
            continue;
          }
          Keep.push_back(I);
        }
        if (DroppedHole && emitKeep(Keep))
          return true;
      }
      if (auto Keep = haydnFormatEKeepOperands(OldDesc, NewDesc, kindOk))
        if (emitKeep(*Keep))
          return true;
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

  // Class-bag reconstruction (skip-Finalize / hand-asm FieldSlot) is
  // deleted. AIE serializes typed members as-is
  // (AIEBaseMCCodeEmitter.cpp:45-68); Haydn keeps only as-is copy,
  // positional promote, and SET_HWLOOP identity. Shape mismatch fails
  // closed rather than rebuilding operands by register class.
  return false;
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
  if (!IsFormatE2 && !IsFormatE3) {
    report_fatal_error(
        "Haydn MC: slot sub-instruction encode only accepts Format E "
        "composites (BUNDLE_E96_*) — legacy composite path retired (FE8)",
        /*GenCrashDiag=*/false);
  }

  const uint8_t Mode = IsFormatE3 ? 1 : 0;
  unsigned EntryWidth = 0;
  unsigned EntryLSB = 0;
  if (!haydnFormatEEntryWindow(Mode, SlotIdx, EntryWidth, EntryLSB)) {
    report_fatal_error(
        Twine("Haydn MC: no generated Format E entry window for mode=") +
            Twine(static_cast<unsigned>(Mode)) + " entry=" + Twine(SlotIdx),
        /*GenCrashDiag=*/false);
  }
  (void)EntryLSB;
  if (isFormatENopOpcode(SubInst.getOpcode(), MII)) {
    Op = APInt(EntryWidth, 0); // zero entry → NOP under Format E inverse
    return;
  }

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
  const unsigned ParcelBits =
      haydn::format::encodedBitsOrDie(haydn::format::BundleFormatRowID::E96TwoEntry)
          .Value;
  APInt Scratch(ParcelBits, 0);
  APInt InstBits(ParcelBits, 0);
  getBinaryCodeForInstr(SubInst, LocalFixups, InstBits, Scratch, STI);
  Op = InstBits.zextOrTrunc(EntryWidth);
  // Member Inst{} may report a mid-parcel field byte (E3 I12 @ bit 23).
  // Product FieldLsb is absolute parcel bits; r_offset stays the parcel
  // origin so applyFixup / lld P is the hardware PC (exact code record).
  // Peer: AIE translateFixupsInComposite emits MCFixup at offset 0
  // (AIEBaseMCCodeEmitter.cpp:231-232).
  for (const MCFixup &F : LocalFixups)
    addHaydnFixup(Fixups, /*Offset=*/0, F.getValue(), F.getKind());
}

//===----------------------------------------------------------------------===//
// Fixup helpers
//===----------------------------------------------------------------------===//

unsigned HaydnMCCodeEmitter::getBranchFixupKind(const MCInst &MI) const {
  // Layout-derived (same lookup as getExprFixupKind). W38: no published
  // row → fatal, never borrow RI12.
  if (std::optional<unsigned> Kind = getExprFixupKind(MI, MII))
    return *Kind;
  report_fatal_error(
      Twine("Haydn MC: no branch fixup kind defined for mnemonic '") +
          MII.getName(MI.getOpcode()) +
          "' (symbolic branch operand on an unhandled opcode)",
      /*GenCrashDiag=*/false);
}

unsigned HaydnMCCodeEmitter::getCallFixupKind(const MCInst &MI) const {
  // Layout-derived. JAL → WIDE_CallSImm20; JALR → JALRSImm12 (dedicated
  // row; W27: never BranchSImm16 / WIDE_BranchSImm12_RI).
  if (std::optional<unsigned> Kind = getExprFixupKind(MI, MII))
    return *Kind;
  report_fatal_error(
      Twine("Haydn MC: no call fixup kind defined for mnemonic '") +
          MII.getName(MI.getOpcode()) +
          "' (symbolic call operand with no published reloc row)",
      /*GenCrashDiag=*/false);
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
    if (std::optional<unsigned> Kind =
            getExprFixupKind(MI, MII, OpNo, MO.getExpr())) {
      addHaydnFixup(Fixups, /*Offset=*/0, MO.getExpr(), *Kind);
    } else {
      // F19: unknown logical must not emit FIXUP_HAYDN_32 (4-byte patch
      // over the Format E header). Diagnose; leave the field zero.
      Ctx.reportError(SMLoc(),
                      Twine("no typed fixup kind for symbolic operand on '") +
                          MII.getName(MI.getOpcode()) +
                          "' — refuse FIXUP_HAYDN_32 header clobber");
    }
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
      // JAL field stores byte PC+imm (no extra scale). Same alignment
      // law as the branch path (MinBundleAddressAlignBytes). Signed
      // 20-bit window; do not silently truncate Imm & 0xFFFFF.
      const unsigned Align = haydn::format::MinBundleAddressAlignBytes;
      if (Imm & (Align - 1))
        Ctx.reportError(SMLoc(), "jal offset must be 2-byte aligned");
      if (!isInt<20>(Imm))
        Ctx.reportError(SMLoc(), "jal offset out of range");
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
  (void)STI;

  if (OpNo >= MI.getNumOperands()) {
    Op = 0;
    return;
  }

  const MCOperand &MO = MI.getOperand(OpNo);

  if (MO.isImm()) {
    int64_t Imm = MO.getImm();
    // AIE AIEBaseMCCodeEmitter.h:127-150 getSImmOpValueXStep: step
    // alignment plus Min/Max from the operand-class width. N/Shift/
    // IsSigned come from the generated operand class, not a hand width.
    const unsigned Align =
        Shift > 0 ? (1u << Shift)
                  : (IsPCRel ? haydn::format::MinBundleAddressAlignBytes : 1u);
    if (Align > 1 && (Imm & static_cast<int64_t>(Align - 1))) {
      if (FixupKind == Haydn::FIXUP_HAYDN_WIDE_CallSImm20)
        Ctx.reportError(SMLoc(), "jal offset must be 2-byte aligned");
      else
        Ctx.reportError(SMLoc(),
                        Twine("offset must be ") + Twine(Align) +
                            "-byte aligned");
    }
    const int64_t Encoded = Imm >> Shift;
    const bool InRange =
        IsSigned ? isInt<N>(Encoded) : isUInt<N>(Encoded);
    if (!InRange) {
      if (FixupKind == Haydn::FIXUP_HAYDN_WIDE_CallSImm20)
        Ctx.reportError(SMLoc(), "jal offset out of range");
      else
        Ctx.reportError(SMLoc(), "immediate operand value is out of range");
    }
    uint64_t Mask = (N >= 64) ? ~0ULL : ((uint64_t{1} << N) - 1);
    Op = static_cast<uint64_t>(Encoded) & Mask;
    return;
  }

  if (MO.isExpr()) {
    // Same layout lookup as getExprFixupKind. Do not fall back to the
    // baked EncoderMethod kind — JALR members use calltarget_wide_ri12
    // (one-reg WIDE_BranchSImm12, not two-reg _RI). Symbolic JALR has
    // no product reloc row and fail-closes.
    if (std::optional<unsigned> Kind =
            getExprFixupKind(MI, MII, OpNo, MO.getExpr())) {
      addHaydnFixup(Fixups, /*Offset=*/0, MO.getExpr(), *Kind);
    } else if (FixupKind != Haydn::FIXUP_HAYDN_NONE) {
      Ctx.reportError(SMLoc(),
                      Twine("no typed fixup kind for symbolic operand on '") +
                          MII.getName(MI.getOpcode()) +
                          "' — refuse FIXUP_HAYDN_32 header clobber");
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

#include "HaydnGenMCCodeEmitter.inc"
