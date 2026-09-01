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
// (generated field geometry + InstBits header indicator 111). The legacy
// composite opcode and encode APIs are deleted — zero residual product emit.
// Empty/idle parcels fail closed until golden idle is registered.
//
// AIE peers (serialize-only model for Format E entry composition):
//   AIEBaseMCCodeEmitter.cpp:45-68  encodeInstruction = getBinaryCode + emit
//   AIEBaseMCCodeEmitter.cpp:122-184 encode nested sub-inst from member Desc
//
// `encodeInstruction` routing (serialize-only / fail-closed):
//   BUNDLE_E96_* product:
//     typed MemberId children -> trySerializeFormatECompositeAsIs (never DFS/fill);
//                               unused entries take generated IsNop records only
//     public logicals         -> standalone one-parcel place (hand-asm only;
//                               assignFormatEMemberEntries never for compiler)
//   Haydn::BUNDLE residual     -> encodeBundle: typed members scatter by
//                               (Mode, EntryIdx) as-is; public logicals fatal
//                               (skip-Finalize never DFS/fill; `_MSP` is not occupancy)
//   standalone real opcode    -> wrap as one Format E row (generated unused-entry NOP)
// Long-branch is CodeGen BranchRelaxation only — no PseudoLongB* / expandLongBranch.
// Placement failure fails closed — no sequential E2 singleton split (layout-size
// fiction vs BranchRelaxation / FixupHwLoops). Typed MemberId composites serialize
// as-is (no Mode/swap retry) and never free-DFS. Bounded one-parcel DFS is
// standalone hand-asm only (golden logical names). Compiler 3-child rebind
// (store at E3 e0 LOADSTORE0; dual loads LS0+LOAD1) lives in Finalize
// assignFormatEMemberEntries. Residual FieldSlots never recover occupancy
// into DFS/fill (AIE MultiSlot alts, AIEMCFormats.h:376-379). Retired slot
// suffixes never pin entries. Occupancy is haydnCatalogOccupancyName for
// standalone public logicals only — compiler MultiSlot `_MSP` clones are not
// occupancy and never peel into DFS.
// encodeSlotSubInst is serialize-only: every non-NOP child must already be a
// generated private member. Compiler BUNDLE_E96_* roots serialize typed
// (row, entry, member) as-is via encodeInstructionFromCompilerRoot and never
// re-enter standalone DFS. fillFormatEMemberInstFromCompilerRoot is fail-closed.
// fillFormatEMemberInst never calls FromRawBundle (compiler-root wall).
// fillFormatEMemberInstFromRawBundle is positional copy only.
// fillFormatEMemberInstPublicHandAsm is standalone keep-map only
// (AR-UA POST / CB / Imm-0); the sole reconstruction caller.
// Closed extra-op keep-map (AR-UA POST, CB writeback, Imm-0 hole, 0-op HINT) is
// standalone only. Compiler extra-op cutover (MOVE32/ABS32 trailing rs2,
// tied MAC acc) stays in Finalize. Residual FieldSlot never enters fill.
// FieldSlot, MemberId, and compiler extra-op never reconstruct.
// Class-bag operand rebuild is deleted.
// Peer: AIEBaseMCCodeEmitter.cpp:45-68 serializes typed members as-is.
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
  // dedicated JALRSImm12 row (never the RI12 branch row).
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

// Residual FieldSlots cannot recover a catalog logical by suffix. AIE
// occupancy is generated MultiSlot alts (AIEMCFormats.h:376-379).
static bool isResidualFieldSlotOpcodeName(StringRef Name) {
  return haydnIsResidualFieldSlotName(Name);
}

// Forward decls for Format E placement (defined with encodeSlotSubInst).
static std::string formatELogicalName(StringRef Name);
static const FormatEMemberRec *findFormatEMemberByOpcode(unsigned Opc);
static bool isFormatENopOpcode(unsigned Opc, const MCInstrInfo &MII);
static unsigned generatedUnusedEntryNopOpcode(uint8_t Mode, unsigned EntryIdx);
/// Compiler MemberId serialize: bind each real by generated (Mode, EntryIdx)
/// and generated unused-entry NOP holes. Never DFS, name peel, or fill.
static bool trySerializeFormatECompositeAsIs(const MCInst &In,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI,
                                             MCInst &Out,
                                             SmallVectorImpl<MCInst> &Storage);
/// Standalone public logicals: haydnSelectStandaloneFormatEOpcode then
/// one-parcel assign (never Mode retry from child count). False = fail
/// closed (never multi-parcel). Committed MemberId never enters here.
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

  // Entry qualification (typed (kind, entry, window) mapping): retarget a
  // base fixup kind to its entry-qualified variant when the member's
  // committed entry is not the base default window. Unmapped sites fatal.
  unsigned qualifyFixupKindForEntry(unsigned Kind, uint8_t Mode,
                                    unsigned EntryIdx,
                                    const FormatEMemberRec &Mem) const;

  // Residual TargetOpcode::BUNDLE: typed MemberId children scatter as-is.
  // Public logicals fatal (skip-Finalize never DFS/fill). Empty/all-NOP uses
  // canonical idle when registered.
  void encodeBundle(const MCInst &MBI, SmallVectorImpl<char> &CB,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const;

  // Compiler-root BUNDLE_E96_*: serialize typed MemberId as-is. Never DFS,
  // name peel, or fill. Peer: AIEBaseMCCodeEmitter.cpp:45-68.
  bool encodeInstructionFromCompilerRoot(const MCInst &MI,
                                         SmallVectorImpl<char> &CB,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const;

  void emitFormatEParcel(const MCInst &Comp, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;

  unsigned getBranchFixupKind(const MCInst &MI) const;
  unsigned getCallFixupKind(const MCInst &MI) const;
};

} // end anonymous namespace

/// Compiler-root wall: never FromRawBundle. Defined later in this TU.
static bool fillFormatEMemberInst(const FormatEMemberRec &Mem,
                                  const MCInst &Logical, const MCInstrInfo &MII,
                                  const MCRegisterInfo &MRI, MCInst &Out);

/// Resolve the generated Format E type record for \p MI from the committed
/// MemberId opcode only. Public-logical name match would pick the first catalog
/// row and invent (row, entry, member, fixup-kind). After placement the SubInst
/// is already a generated member. Peer: AIE getBinaryCodeForInstr on member Desc
/// (AIEBaseMCCodeEmitter.cpp:45-68 / 134-162).
static const FormatEMemberRec *
resolveFormatETypeMember(const MCInst &MI, const MCInstrInfo &MII) {
  (void)MII;
  return findFormatEMemberByOpcode(MI.getOpcode());
}

// Determine the appropriate fixup kind for an expression operand.
// MCSpecifierExpr (%hi12/%lo20/%pc_lo20) selects the kind (R12). Bare
// symbols use AIE findFixupfromFixupFields against HaydnRelocLayout —
// generated TypeName + type-opcode + field size, not a logical-name
// switch. Unknown types return nullopt. Callers refuse FIXUP_HAYDN_32.
// JALR (RI12 opc 1) resolves to the dedicated JALRSImm12 row (rs+imm12;
// never borrows the PC-rel branch row).
// OpNo selects HWLoop Off1 (6-bit) vs Off2 (12-bit); unknown → Off1.
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
    // Full-bundle architectural NOP is the product idle parcel (header 111
    // plus zero entries). All-zero (indicator 000) is not Format E.
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

    // One compiler cycle → one E96 parcel. Never split into sequential E2
    // singletons (that rewrote layout size after BranchRelaxation /
    // FixupHwLoops). Committed MemberId children serialize as-is and never
    // enter standalone DFS / fill. Public logicals (hand-asm) one-parcel DFS.
    SmallVector<MCInst, 4> PlaceStorage;
    MCInst Placed;

    // Isolate committed MemberId as-is from standalone DFS: any private
    // child means serialize-only (AIEBaseMCCodeEmitter.cpp:45-68). Mixed
    // private+logical is a skip-Finalize leak — never fill/peel.
    bool AnyPrivate = false;
    bool AnyPublicReal = false;
    for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
      const MCOperand &Op = MI.getOperand(I);
      if (!Op.isInst() || !Op.getInst())
        continue;
      const unsigned ChildOpc = Op.getInst()->getOpcode();
      if (isFormatENopOpcode(ChildOpc, MII))
        continue;
      if (isResidualFieldSlotOpcodeName(MII.getName(ChildOpc))) {
        report_fatal_error(
            Twine("Haydn MC: residual FieldSlot '") +
                MII.getName(ChildOpc) +
                "' cannot enter standalone DFS / bag-sort — refuse name peel",
            /*GenCrashDiag=*/false);
      }
      if (findFormatEMemberByOpcode(ChildOpc))
        AnyPrivate = true;
      else
        AnyPublicReal = true;
    }
    if (AnyPrivate) {
      if (AnyPublicReal) {
        report_fatal_error(
            "Haydn MC: committed Format E private member cannot enter "
            "standalone DFS re-place — refuse name recovery / row retry "
            "(serialize-only)",
            /*GenCrashDiag=*/false);
      }
      if (!encodeInstructionFromCompilerRoot(MI, CB, Fixups, STI)) {
        report_fatal_error(
            "Haydn MC: committed Format E private member cannot enter "
            "standalone DFS re-place — refuse name recovery / row retry "
            "(serialize-only)",
            /*GenCrashDiag=*/false);
      }
      return;
    }

    // Standalone/hand-asm public logicals only. Compiler TargetOpcode::BUNDLE
    // residuals never reach here (encodeBundle wall).
    if (buildFormatEPlacedComposite(MI, MII, *Ctx.getRegisterInfo(), Placed,
                                    PlaceStorage)) {
      emitFormatEParcel(Placed, CB, Fixups, STI);
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
      if (isResidualFieldSlotOpcodeName(RawName)) {
        report_fatal_error(
            Twine("Haydn MC: residual FieldSlot '") + RawName +
                "' cannot enter standalone DFS / bag-sort — refuse name "
                "peel; children=[" +
                ChildDiag + "]",
            /*GenCrashDiag=*/false);
      }
      if (isFormatENopOpcode(ChildOpc, MII))
        continue;
      // Empty occupancy is skip-Finalize / `_MSP` clone / unknown — never a
      // silent pad. Hand-asm DFS needs a catalog logical.
      if (Log.empty() || StringRef(Log).equals_insensitive("NOP")) {
        report_fatal_error(
            Twine("Haydn MC: Format E composite child '") + RawName +
                "' is not a standalone catalog logical — refuse skip-Finalize "
                "DFS / occupancy peel; children=[" +
                ChildDiag + "]",
            /*GenCrashDiag=*/false);
      }
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
    // Reaching here means as-is MemberId serialize failed entirely.
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
  // wrap as the generated standalone row (NOP in unused entries). Matches
  // AsmParser haydnSelectStandaloneFormatEOpcode (AIE PacketFormats
  // first-covering, AIEBaseAsmParser.h:164-180) — never TWO vs THREE from
  // child count. Idle bare NOP remains fail-closed until golden idle is
  // registered. Residual FieldSlots never wrap into DFS / fill (AIE
  // MultiSlot alts, AIEMCFormats.h:376-379). Private members wrap by their
  // generated Mode and serialize as-is (AIEBaseMCCodeEmitter.cpp:45-68).
  if (isResidualFieldSlotOpcodeName(MII.getName(MI.getOpcode()))) {
    report_fatal_error(
        Twine("Haydn MC: residual FieldSlot '") +
            MII.getName(MI.getOpcode()) +
            "' cannot enter standalone DFS / bag-sort — refuse name peel",
        /*GenCrashDiag=*/false);
  }
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

  unsigned CompOpc = 0;
  if (const FormatEMemberRec *Mem =
          findFormatEMemberByOpcode(MI.getOpcode())) {
    CompOpc = Mem->Mode ? Haydn::BUNDLE_E96_THREE_ENTRY
                        : Haydn::BUNDLE_E96_TWO_ENTRY;
  } else {
    const unsigned BareOpc = MI.getOpcode();
    CompOpc = haydnSelectStandaloneFormatEOpcode(BareOpc);
  }
  if (CompOpc != Haydn::BUNDLE_E96_TWO_ENTRY &&
      CompOpc != Haydn::BUNDLE_E96_THREE_ENTRY) {
    report_fatal_error(
        Twine("Haydn MC: standalone opcode '") + MII.getName(MI.getOpcode()) +
            "' has no generated Format E row — refuse child-count wrap",
        /*GenCrashDiag=*/false);
  }
  MCInst Nop;
  Nop.setOpcode(Haydn::NOP);
  MCInst Comp;
  Comp.setOpcode(CompOpc);
  Comp.addOperand(MCOperand::createInst(&MI));
  const unsigned EntryCount =
      CompOpc == Haydn::BUNDLE_E96_THREE_ENTRY ? 3u : 2u;
  for (unsigned E = 1; E < EntryCount; ++E)
    Comp.addOperand(MCOperand::createInst(&Nop));
  encodeInstruction(Comp, CB, Fixups, STI);
}

void HaydnMCCodeEmitter::emitFormatEParcel(
    const MCInst &Comp, SmallVectorImpl<char> &CB,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
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
}

bool HaydnMCCodeEmitter::encodeInstructionFromCompilerRoot(
    const MCInst &MI, SmallVectorImpl<char> &CB,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
  // Typed MemberId only. Residual FieldSlot and public logicals never
  // serialize here — encodeInstruction fatals those before this call, and
  // this returns false as a second wall (never DFS / fill).
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    const MCOperand &Op = MI.getOperand(I);
    if (!Op.isInst() || !Op.getInst())
      continue;
    const unsigned ChildOpc = Op.getInst()->getOpcode();
    if (isFormatENopOpcode(ChildOpc, MII))
      continue;
    if (isResidualFieldSlotOpcodeName(MII.getName(ChildOpc)))
      return false;
    const FormatEMemberRec *Mem = findFormatEMemberByOpcode(ChildOpc);
    if (!Mem)
      return false;
    // fillFormatEMemberInst is the compiler-root wall: it never calls
    // FromRawBundle. Success here would mean reconstruction leaked in.
    MCInst Discard;
    if (fillFormatEMemberInst(*Mem, *Op.getInst(), MII, *Ctx.getRegisterInfo(),
                              Discard))
      return false;
  }
  SmallVector<MCInst, 4> PlaceStorage;
  MCInst Placed;
  if (!trySerializeFormatECompositeAsIs(MI, MII, *Ctx.getRegisterInfo(),
                                        Placed, PlaceStorage))
    return false;
  emitFormatEParcel(Placed, CB, Fixups, STI);
  return true;
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

  // Compiler generic BUNDLE: typed MemberId children wrap and serialize
  // as-is. Residual logicals are skip-Finalize — refuse DFS
  // fillFormatEMemberInst. Finalize owns keep-map extra-op cutover
  // (MOVE32 3-op vs member 2-op). Standalone hand-asm is a bare opcode
  // or BUNDLE_E96_* of public logicals, not TargetOpcode::BUNDLE.
  // Peer: AIEBaseMCCodeEmitter.cpp:45-68 serializes typed members as-is.
  for (const MCInst *C : Children) {
    if (findFormatEMemberByOpcode(C->getOpcode()))
      continue;
    report_fatal_error(
        Twine("Haydn MC: compiler BUNDLE child '") +
            MII.getName(C->getOpcode()) +
            "' is not a generated Format E member — refuse skip-Finalize "
            "DFS / bag-sort",
        /*GenCrashDiag=*/false);
  }

  // Scatter typed members by generated (Mode, EntryIdx). Child count is not
  // row identity: two E3 members plus a filtered NOP pad is still E3.
  // Holes are encode-time NOP. Peer: AIE serializes the packet as-is
  // (AIEBaseMCCodeEmitter.cpp:45-68); members already carry slot identity.
  std::optional<uint8_t> Mode;
  const MCInst *ChildAt[3] = {nullptr, nullptr, nullptr};
  for (const MCInst *C : Children) {
    const FormatEMemberRec *Mem = findFormatEMemberByOpcode(C->getOpcode());
    if (!Mem)
      report_fatal_error(
          Twine("Haydn MC: compiler BUNDLE child '") +
              MII.getName(C->getOpcode()) +
              "' lost MemberId after typed check",
          /*GenCrashDiag=*/false);
    if (Mode && *Mode != Mem->Mode)
      report_fatal_error(
          "Haydn MC: compiler BUNDLE members disagree on Mode — refuse "
          "count wrap / DFS",
          /*GenCrashDiag=*/false);
    Mode = Mem->Mode;
    if (Mem->EntryIdx > 2 || ChildAt[Mem->EntryIdx])
      report_fatal_error(
          Twine("Haydn MC: compiler BUNDLE EntryIdx collision for '") +
              MII.getName(C->getOpcode()) + "'",
          /*GenCrashDiag=*/false);
    ChildAt[Mem->EntryIdx] = C;
  }
  const unsigned EntryCount = *Mode ? 3u : 2u;
  for (unsigned E = 0; E < 3; ++E) {
    if (ChildAt[E] && E >= EntryCount)
      report_fatal_error(
          "Haydn MC: compiler BUNDLE member EntryIdx exceeds Mode width",
          /*GenCrashDiag=*/false);
  }
  MCInst Pad0, Pad1, Pad2;
  MCInst *Pads[3] = {&Pad0, &Pad1, &Pad2};
  for (unsigned E = 0; E < EntryCount; ++E) {
    if (ChildAt[E])
      continue;
    const unsigned NopOpc = generatedUnusedEntryNopOpcode(*Mode, E);
    if (!NopOpc)
      report_fatal_error(
          "Haydn MC: no generated unused-entry NOP record for compiler BUNDLE "
          "pad — refuse occupancy invent",
          /*GenCrashDiag=*/false);
    Pads[E]->setOpcode(NopOpc);
  }
  MCInst Comp;
  Comp.setOpcode(*Mode ? Haydn::BUNDLE_E96_THREE_ENTRY
                       : Haydn::BUNDLE_E96_TWO_ENTRY);
  for (unsigned E = 0; E < EntryCount; ++E)
    Comp.addOperand(
        MCOperand::createInst(ChildAt[E] ? ChildAt[E] : Pads[E]));
  // Compiler-root serialize as-is. Never re-enter encodeInstruction DFS/fill.
  if (!encodeInstructionFromCompilerRoot(Comp, CB, Fixups, STI))
    report_fatal_error(
        "Haydn MC: compiler BUNDLE MemberId serialize failed — refuse "
        "skip-Finalize DFS / bag-sort",
        /*GenCrashDiag=*/false);
}

//===----------------------------------------------------------------------===//
// encodeSlotSubInst — serialize one placed Format E entry (generated emitter)
//===----------------------------------------------------------------------===//
//
// AIEBaseMCCodeEmitter.cpp:134-162 encodes SubInst Desc as-is;
// SubInstFormat/slot geometry come from SubInst.getOpcode().
//
// Placement emits generated private members or NOP. This function serializes
// that Desc. Bare logical children are a placement bug — fail closed, no
// name peel or rebuild. MemberId → Haydn::<E96 member opcode>.
// File scope so typed serialize/lookup and standalone fill share one map.
#define GET_FORMAT_E_MEMBER_OPCODES
#include "HaydnGenFormatEMemberOpcodes.inc"

// File-scope forward decls: standalone DFS fill (defined later in this TU).
// encodeSlotSubInst never calls this — placement only.
static bool isCompilerKeepMapExtraOp(const FormatEMemberRec &Mem,
                                     const MCInst &Logical,
                                     const MCInstrInfo &MII);
static bool isSkipFinalizeFillRefuse(const FormatEMemberRec &Mem,
                                     const MCInst &Logical,
                                     const MCInstrInfo &MII);
static bool fillFormatEMemberInstFromCompilerRoot(
    const FormatEMemberRec &Mem, const MCInst &Logical, const MCInstrInfo &MII,
    const MCRegisterInfo &MRI, MCInst &Out);
static bool fillFormatEMemberInstFromRawBundle(const FormatEMemberRec &Mem,
                                               const MCInst &Logical,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI,
                                               MCInst &Out);
static bool fillFormatEMemberInstPublicHandAsm(const FormatEMemberRec &Mem,
                                               const MCInst &Logical,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI,
                                               MCInst &Out);
static bool fillFormatEMemberInst(const FormatEMemberRec &Mem,
                                  const MCInst &Logical, const MCInstrInfo &MII,
                                  const MCRegisterInfo &MRI, MCInst &Out);

// Same TU anonymous namespace as the emitter class (C++ merges them).
namespace {

// Residual hand-asm: matcher may still match the logical public mnemonic.
// Materialize that residual via the generated member table. Local copy only;
// no MCFlags.
static bool isFormatENopOpcode(unsigned Opc, const MCInstrInfo &MII) {
  if (Opc == Haydn::NOP)
    return true;
  StringRef Name = MII.getName(Opc);
  // Residual FieldSlots are not NOP via suffix peel.
  if (isResidualFieldSlotOpcodeName(Name))
    return false;
  // Generated members: IsNop from the member table, never a name peel.
  // Public logicals (including MultiSlot `_MSP` clones) are never encode-time
  // pads — occupancy empty is skip-Finalize, not NOP.
  if (!haydnIsGeneratedMemberName(Name))
    return false;
  for (unsigned I = 0; I < FormatEMemberOpcodeCount; ++I) {
    if (FormatEMemberOpcodes[I] != Opc)
      continue;
    return I < haydn::format_e::FormatEMemberCount &&
           haydn::format_e::FormatEMembers[I].IsNop;
  }
  return false;
}

/// Unused-entry pad opcode from a generated IsNop record at (Mode, EntryIdx).
/// Product NOP members alias Haydn::NOP in FormatEMemberOpcodes; the record
/// is the authority that the unused entry exists. 0 = fail closed (no invent).
static unsigned generatedUnusedEntryNopOpcode(uint8_t Mode, unsigned EntryIdx) {
  unsigned Hint = 0;
  unsigned Any = 0;
  for (unsigned I = 0; I < haydn::format_e::FormatEMemberCount; ++I) {
    const FormatEMemberRec &Mem = haydn::format_e::FormatEMembers[I];
    if (!Mem.IsNop || Mem.Mode != Mode || Mem.EntryIdx != EntryIdx)
      continue;
    if (I >= FormatEMemberOpcodeCount)
      continue;
    const unsigned Opc = FormatEMemberOpcodes[I];
    if (Opc == 0)
      continue;
    if (Mem.TypeName && StringRef(Mem.TypeName).equals_insensitive("HINT") &&
        Hint == 0)
      Hint = Opc;
    else if (Any == 0)
      Any = Opc;
  }
  return Hint ? Hint : Any;
}

/// Typed reverse map: private Format E member opcode → generated MemberId.
static const FormatEMemberRec *findFormatEMemberByOpcode(unsigned Opc) {
  return haydnFindFormatEMemberByOpcode(Opc);
}

/// Serialize-as-is when every real is a typed private Format E member
/// fixed at its composite entry under Mode (0=E2, 1=E3) from the product
/// composite opcode. Bind by MemberId + Mode/EntryIdx. Positional full-slot
/// packs with zero-entry NOP pads serialize as-is. Private members never DFS.
/// encodeSlotSubInst never peels or rebuilds.
static bool trySerializeFormatECompositeAsIs(const MCInst &In,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI,
                                             MCInst &Out,
                                             SmallVectorImpl<MCInst> &Storage) {
  const bool IsE3 = In.getOpcode() == Haydn::BUNDLE_E96_THREE_ENTRY;
  const bool IsE2 = In.getOpcode() == Haydn::BUNDLE_E96_TWO_ENTRY;
  if (!IsE2 && !IsE3)
    return false;
  const uint8_t Mode = static_cast<uint8_t>(IsE3 ? 1 : 0);
  const unsigned EntryCount = Mode ? 3u : 2u;
  // Operand count is occupancy, not row width. Missing entries are
  // encode-time CompletionState NOP (AIEBaseMCCodeEmitter.cpp:45-68
  // serializes the packet as-is and pads empty slots).
  (void)MRI;

  SmallVector<const MCInst *, 3> ChildAt(EntryCount, nullptr);
  unsigned RealCount = 0;
  uint32_t UsedUnits = 0;
  // Collect real children in composite operand order (skip NOP).
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
        const unsigned NopOpc = generatedUnusedEntryNopOpcode(Mode, E);
        if (!NopOpc)
          return false;
        Storage.emplace_back();
        Storage.back().setOpcode(NopOpc);
        Out.addOperand(MCOperand::createInst(&Storage.back()));
        continue;
      }
      Out.addOperand(MCOperand::createInst(ChildAt[E]));
    }
    return true;
  }

  // Bare logicals (hand-asm) fall through to one-parcel DFS in
  // buildFormatEPlacedComposite.
  return false;
}

/// Standalone/hand-asm one-parcel DFS only. Committed MemberId composites
/// never enter this function (encodeInstruction as-is wall + encodeBundle).
/// Row from haydnSelectStandaloneFormatEOpcode, then assignFormatEMemberEntries.
/// Peer: AIE emitBundle first-covering (AIEBaseAsmParser.h:164-180).
static bool buildFormatEPlacedComposite(const MCInst &In,
                                        const MCInstrInfo &MII,
                                        const MCRegisterInfo &MRI, MCInst &Out,
                                        SmallVectorImpl<MCInst> &Storage) {
  // Defense: typed private members serialize as-is in encodeInstruction.
  // Refuse leakage into name-recovery DFS.
  bool SawPrivate = false;
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
  }
  if (SawPrivate)
    return false; // never DFS a composite that carries typed private members

  SmallVector<const MCInst *, 3> Reals;
  SmallVector<std::string, 3> LogicalNames;
  for (unsigned I = 0, E = In.getNumOperands(); I != E; ++I) {
    const MCOperand &Op = In.getOperand(I);
    if (!Op.isInst() || !Op.getInst())
      continue;
    const unsigned ChildOpc = Op.getInst()->getOpcode();
    StringRef RawName = MII.getName(ChildOpc);
    // Residual FieldSlots are not standalone occupancy. Do not peel `_S*`
    // into a catalog logical and bag-sort (AIEMCFormats.h:376-379).
    if (isResidualFieldSlotOpcodeName(RawName))
      return false;
    if (isFormatENopOpcode(ChildOpc, MII))
      continue;
    std::string Log = formatELogicalName(RawName);
    // `_MSP` / unknown occupancy is skip-Finalize, not a silent pad.
    if (Log.empty() || StringRef(Log).equals_insensitive("NOP"))
      return false;
    Reals.push_back(Op.getInst());
    LogicalNames.push_back(std::move(Log));
  }
  if (Reals.empty() || Reals.size() > 3)
    return false;

  // Standalone hand-asm only. Row identity is haydnSelectStandaloneFormatEOpcode
  // (generated Mode-only membership + unit cover + EntryCapacity), the same
  // PacketFormats first-covering the parser uses. Never retry the other Mode
  // because Reals.size() <= 2/<= 3 — child count is occupancy, not TWO vs
  // THREE. Peer: AIE emitBundle (AIEBaseAsmParser.h:164-180).
  SmallVector<unsigned, 3> RealOpcs;
  RealOpcs.reserve(Reals.size());
  for (const MCInst *C : Reals)
    RealOpcs.push_back(C->getOpcode());
  const unsigned Selected = haydnSelectStandaloneFormatEOpcode(RealOpcs);
  if (Selected != Haydn::BUNDLE_E96_TWO_ENTRY &&
      Selected != Haydn::BUNDLE_E96_THREE_ENTRY)
    return false;
  const uint8_t BestMode =
      Selected == Haydn::BUNDLE_E96_THREE_ENTRY ? 1 : 0;
  auto Best = assignFormatEMemberEntries(LogicalNames, BestMode);
  if (!Best)
    return false;

  const unsigned EntryCount = BestMode ? 3u : 2u;
  Out.clear();
  Out.setOpcode(BestMode ? Haydn::BUNDLE_E96_THREE_ENTRY
                         : Haydn::BUNDLE_E96_TWO_ENTRY);

  SmallVector<int, 3> KidAtEntry(EntryCount, -1);
  for (unsigned K = 0, KE = Reals.size(); K != KE; ++K)
    KidAtEntry[(*Best)[K].EntryIdx] = static_cast<int>(K);

  // Skip-Finalize extra-op / FieldSlot / MemberId discard occupancy and
  // never start fill or rebuild operands.
  for (unsigned K = 0, KE = Reals.size(); K != KE; ++K) {
    if (!(*Best)[K].Mem ||
        isSkipFinalizeFillRefuse(*(*Best)[K].Mem, *Reals[K], MII))
      return false;
  }

  Storage.clear();
  Storage.reserve(EntryCount);
  for (unsigned E = 0; E < EntryCount; ++E) {
    if (KidAtEntry[E] >= 0) {
      const int Kid = KidAtEntry[E];
      if (!(*Best)[Kid].Mem)
        return false;
      Storage.emplace_back();
      // Skip-Finalize compiler extras / FieldSlot / MemberId never enter
      // fill (no bag-sort, no operand rebuild). Standalone public logicals
      // reconstruct only through PublicHandAsm (positional / Imm-0 /
      // AR-UA POST / CB). fillFormatEMemberInst never calls FromRawBundle.
      // Peer: AIEBaseMCCodeEmitter.cpp:45-68 serializes typed members as-is.
      if (isSkipFinalizeFillRefuse(*(*Best)[Kid].Mem, *Reals[Kid], MII))
        return false;
      if (!fillFormatEMemberInstPublicHandAsm(*(*Best)[Kid].Mem, *Reals[Kid],
                                             MII, MRI, Storage.back()))
        return false;
      Out.addOperand(MCOperand::createInst(&Storage.back()));
    } else {
      const unsigned NopOpc = generatedUnusedEntryNopOpcode(BestMode, E);
      if (!NopOpc)
        return false;
      Storage.emplace_back();
      Storage.back().setOpcode(NopOpc);
      Out.addOperand(MCOperand::createInst(&Storage.back()));
    }
  }
  return true;
}

// Catalog / alias occupancy for standalone DFS only. Residual FieldSlots
// and generated members do not recover a logical by suffix peel
// (AIEMCFormats.h:376-379). Compiler MultiSlot `_MSP` clones are not a
// catalog occupancy key — they serialize as MemberId after Finalize, or
// fatal as skip-Finalize. haydnCatalogOccupancyName is the one map for
// public hand-asm names (`_W` compact span stays occupancy, not peel).
static std::string formatELogicalName(StringRef Name) {
  if (isResidualFieldSlotOpcodeName(Name) || haydnIsGeneratedMemberName(Name) ||
      Name.ends_with("_MSP"))
    return {};
  return haydnCatalogOccupancyName(Name);
}

// Entry field packing is TableGen Inst{} on live Format E members
// (HaydnFormatsE96Members.td.inc). encodeSlotSubInst serializes a placed
// member MCInst via getBinaryCodeForInstr — do not reintroduce hand field
// packers or fillFormatEMemberInst.

} // end anonymous namespace (Format E placement helpers)

/// Finalize keep-map extras: MOVE32/ABS32 trailing rs2 and tied MAC/MOVT
/// acc. Same NumDefs, more logical ops than the generated member. AR-UA
/// POST / CB writeback change NumDefs and stay in standalone fill. Skip-
/// Finalize compiler extras never enter fillFormatEMemberInst bag-sort.
/// Peer: AIE serializes typed members as-is (AIEBaseMCCodeEmitter.cpp:45-68).
static bool isCompilerKeepMapExtraOp(const FormatEMemberRec &Mem,
                                     const MCInst &Logical,
                                     const MCInstrInfo &MII) {
  return haydnIsCompilerKeepMapExtraOp(Mem, Logical, MII);
}

/// Skip-Finalize / compiler-root shapes never enter standalone fill.
/// FieldSlot, committed MemberId, and Finalize extra-op keep-map
/// (MOVE32 trailing, tied MAC, LUI vestigial $rs) fail closed here so
/// fillFormatEMemberInst is not reachable from those packets.
/// Peer: AIEBaseMCCodeEmitter.cpp:45-68 serializes typed members as-is.
static bool isSkipFinalizeFillRefuse(const FormatEMemberRec &Mem,
                                     const MCInst &Logical,
                                     const MCInstrInfo &MII) {
  if (isResidualFieldSlotOpcodeName(MII.getName(Logical.getOpcode())))
    return true;
  if (findFormatEMemberByOpcode(Logical.getOpcode()))
    return true;
  if (Mem.MemberId < FormatEMemberOpcodeCount &&
      FormatEMemberOpcodes[Mem.MemberId] != 0 &&
      Logical.getOpcode() == FormatEMemberOpcodes[Mem.MemberId])
    return true;
  const MCInstrDesc &LogDesc = MII.get(Logical.getOpcode());
  if (Logical.getNumOperands() > LogDesc.getNumOperands())
    return true;
  return isCompilerKeepMapExtraOp(Mem, Logical, MII);
}

/// Compiler-root fill is deleted. MemberId composites serialize as-is
/// (trySerializeFormatECompositeAsIs / encodeInstructionFromCompilerRoot).
/// Extra-op keep-map is Finalize. Residual FieldSlot never fills.
/// Always false — never bag-sort / name peel / operand rebuild.
/// Peer: AIEBaseMCCodeEmitter.cpp:45-68 serializes typed members as-is.
static bool fillFormatEMemberInstFromCompilerRoot(
    const FormatEMemberRec &Mem, const MCInst &Logical, const MCInstrInfo &MII,
    const MCRegisterInfo &MRI, MCInst &Out) {
  (void)Mem;
  (void)Logical;
  (void)MII;
  (void)MRI;
  (void)Out;
  return false;
}

/// Raw/hand-asm positional copy only. Never keep-map reconstruction — that
/// lives in fillFormatEMemberInstPublicHandAsm. Never FieldSlot, never
/// MemberId, never compiler extra-op. encodeSlotSubInst never calls this.
static bool fillFormatEMemberInstFromRawBundle(const FormatEMemberRec &Mem,
                                               const MCInst &Logical,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI,
                                               MCInst &Out) {
  // Residual FieldSlots, committed MemberId, compiler extra-op, and
  // extra operands past the public logical Desc never bag-sort here.
  if (isSkipFinalizeFillRefuse(Mem, Logical, MII))
    return false;
  return haydnFillFormatEMemberInstPositional(Mem, Logical, MII, MRI, Out);
}

/// Standalone/hand-asm only. Positional copy, then closed keep-map (AR-UA
/// POST, CB writeback, Imm-0 hole, 0-op HINT). Sole reconstruction caller.
/// Skip-Finalize compiler extras / FieldSlot / MemberId refuse before fill.
/// FieldSlot, MemberId, and compiler extra-op never reconstruct.
/// Peer: AIEBaseMCCodeEmitter.cpp:45-68 serializes typed members as-is.
static bool fillFormatEMemberInstPublicHandAsm(const FormatEMemberRec &Mem,
                                               const MCInst &Logical,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI,
                                               MCInst &Out) {
  if (isSkipFinalizeFillRefuse(Mem, Logical, MII))
    return false;
  if (fillFormatEMemberInstFromRawBundle(Mem, Logical, MII, MRI, Out))
    return true;
  return haydnFillFormatEMemberInst(Mem, Logical, MII, MRI, Out);
}

/// Public wrapper: skip-Finalize / compiler-root never fill. Never calls
/// FromRawBundle — reconstruction is PublicHandAsm only (standalone
/// buildFormatEPlacedComposite). Public-logical / private-member firewall:
///   * typed members serialize as-is (trySerializeFormatECompositeAsIs)
///   * compiler TargetOpcode::BUNDLE residual logicals fatal in encodeBundle
///   * encodeSlotSubInst never calls this (serialize-only)
///   * committed MemberId opcodes return false (never as-is copy here)
///   * MOVE32/ABS32 trailing extra and tied MAC extra are Finalize keep-map,
///     not bag-sort reconstruction — isCompilerKeepMapExtraOp refuses fill
/// Hand-asm MOVE32 AsmString is 2-op and matches positional in FromRawBundle.
/// Peer: AIE serializes typed members as-is (AIEBaseMCCodeEmitter.cpp:45-68).
static bool fillFormatEMemberInst(const FormatEMemberRec &Mem,
                                  const MCInst &Logical, const MCInstrInfo &MII,
                                  const MCRegisterInfo &MRI, MCInst &Out) {
  if (Mem.MemberId >= FormatEMemberOpcodeCount)
    return fillFormatEMemberInstFromCompilerRoot(Mem, Logical, MII, MRI, Out);
  const unsigned MemberOpc = FormatEMemberOpcodes[Mem.MemberId];
  if (MemberOpc == 0)
    return fillFormatEMemberInstFromCompilerRoot(Mem, Logical, MII, MRI, Out);
  if (isSkipFinalizeFillRefuse(Mem, Logical, MII))
    return fillFormatEMemberInstFromCompilerRoot(Mem, Logical, MII, MRI, Out);
  // Matching public logicals still do not FromRawBundle: reconstruction
  // is PublicHandAsm only.
  return fillFormatEMemberInstFromCompilerRoot(Mem, Logical, MII, MRI, Out);
}

void HaydnMCCodeEmitter::encodeSlotSubInst(
    const MCInst &Composite, const MCInst &SubInst, APInt &Op,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
  // Format E product composites only. Entry width is the generated type-layout
  // window (haydnFormatEEntryWindow + static_assert in HaydnMCFormats.cpp).
  // Member Inst stores the entry in the low bits (HaydnEntryE2*/E3* hi_pad),
  // so composite-absolute LSB is unused here. Peer: AIE extracts
  // Binary.extractBits(SlotInfo->getSize(), StartPos)
  // (AIEBaseMCCodeEmitter.cpp:178-184). Non-Format-E composites fatal.
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
        "composites (BUNDLE_E96_*) — legacy composite path retired",
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
  // EntryLSB is composite-absolute; member Inst is already window-relative.
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
  // for this (Mode, Entry). Placement owns MemberId bind and standalone DFS
  // fill. Name peel / fillFormatEMemberInst are not reachable here.
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
  //
  // ENTRY QUALIFICATION (typed (kind, entry, window) mapping): a parcel can
  // carry two same-kind symbolic fields (e.g. ADDI32 RI20 at E2 e0 ALU0 and
  // E2 e1 ALU1). The base kinds resolve their window by sniffing parcel
  // content, which is ambiguous in that case — one imm gets patched twice,
  // the other never. When this member's committed entry is NOT the base
  // kind's default window, retarget the fixup to the entry-qualified kind
  // so the patch window rides the TYPE (r_offset stays the parcel origin).
  for (const MCFixup &F : LocalFixups)
    addHaydnFixup(Fixups, /*Offset=*/0, F.getValue(),
                  qualifyFixupKindForEntry(F.getKind(), Mode, SlotIdx, *Typed));
}

// Retarget a base fixup kind to its entry-qualified variant when the
// member's committed (Mode, EntryIdx) is not the base default window.
// Unmapped (kind, entry) sites fail closed: no silent e0 patch of a
// non-e0 field (the dual-RI20 mislink class).
static unsigned SubInstOpcodeForMember(const FormatEMemberRec &Mem) {
  return Mem.MemberId < FormatEMemberOpcodeCount
             ? FormatEMemberOpcodes[Mem.MemberId]
             : Haydn::NOP;
}

unsigned HaydnMCCodeEmitter::qualifyFixupKindForEntry(unsigned Kind,
                                                      uint8_t Mode,
                                                      unsigned EntryIdx,
                                                      const FormatEMemberRec &Mem) const {
  using RK = HaydnReloc::RelocKind;
  const RK R = HaydnReloc::mapFixupKind(Kind);
  switch (R) {
  case RK::LO20:
  case RK::PC_LO20:
    // RI20 is E2-only; the non-default entry is E2 e1 ALU1 (@65).
    if (Mode == 0 && EntryIdx == 1)
      return R == RK::LO20 ? Haydn::FIXUP_HAYDN_LO20_E1
                           : Haydn::FIXUP_HAYDN_PC_LO20_E1;
    return Kind;
  case RK::WIDE_CallSImm20:
    if (Mode == 1 && EntryIdx == 1)
      return Haydn::FIXUP_HAYDN_WIDE_CallSImm20_E3E1;
    return Kind;
  case RK::WIDE_BranchSImm12:
    if (Mode == 1 && EntryIdx == 0)
      return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_E3E0;
    if (Mode == 1 && EntryIdx == 1)
      return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_E3E1;
    if (Mode == 1 && EntryIdx == 2)
      return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_E3E2;
    return Kind;
  case RK::WIDE_BranchSImm12_RI:
    if (Mode == 1 && EntryIdx == 0)
      return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI_E3E0;
    if (Mode == 1 && EntryIdx == 1)
      return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI_E3E1;
    return Kind;
  case RK::JALRSImm12:
    if (Mode == 1 && EntryIdx == 0)
      return Haydn::FIXUP_HAYDN_JALRSImm12_E3E0;
    if (Mode == 1 && EntryIdx == 1)
      return Haydn::FIXUP_HAYDN_JALRSImm12_E3E1;
    return Kind;
  default:
    // Uniqueness-by-law kinds keep the base kind at any entry: content
    // sniffing is deterministic while at most ONE such field can exist in
    // a parcel — HI12 (LUI alone-in-cycle, LuiAddiE0), CSR_UImm8 (SFR is
    // single-writer per cycle), HWLoopOff1/2 (SET_HWLOOP is
    // serialized against CSRW by the same-cycle law). LS_IMM is the
    // exception: two symbolic loads (LOADSTORE0 + LOAD1) CAN share a
    // parcel with identical kind, so a non-default entry is ambiguous —
    // fail closed until an entry-qualified LS row exists.
    if (EntryIdx != 0 && R == RK::LS_IMM) {
      report_fatal_error(
          Twine("Haydn MC: symbolic member '") +
              MII.getName(SubInstOpcodeForMember(Mem)) +
              "' carries LS_IMM at a non-default entry (mode=" + Twine(Mode) +
              " entry=" + Twine(EntryIdx) +
              ") with no entry-qualified row — dual symbolic loads in one "
              "parcel would sniff-ambiguate; refuse",
          /*GenCrashDiag=*/false);
    }
    return Kind;
  }
}

//===----------------------------------------------------------------------===//
// Fixup helpers
//===----------------------------------------------------------------------===//

unsigned HaydnMCCodeEmitter::getBranchFixupKind(const MCInst &MI) const {
  // Layout-derived (same lookup as getExprFixupKind). No published
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
  // row; never BranchSImm16 / WIDE_BranchSImm12_RI).
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
    // Hexagon-style kind→PCRel (addHaydnFixup). Generated JAL members
    // route here (getMachineOpValue), not getCallTargetOpValue.
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
      // Unknown logical must not emit FIXUP_HAYDN_32 (4-byte patch
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

  // Branches store byte PC+imm (no extra scale). Bundle min-align is 2.
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
  // Typed I20 type-opcode 1 is JAL (NOP is I20 opcode 0). Do not peel
  // Mem->Logical — that invents identity from a public name.
  bool IsJAL = Opcode == Haydn::JAL;
  if (!IsJAL) {
    if (const FormatEMemberRec *Mem = findFormatEMemberByOpcode(Opcode))
      IsJAL = Mem->TypeName && StringRef(Mem->TypeName) == "I20" &&
              Mem->Opcode == 1;
  }

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
    // baked EncoderMethod kind. calltarget_wide_ri12 is the one-reg JALR
    // window (not two-reg WIDE_BranchSImm12_RI). RI12 type-opcode 1 maps
    // to JALRSImm12.
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
