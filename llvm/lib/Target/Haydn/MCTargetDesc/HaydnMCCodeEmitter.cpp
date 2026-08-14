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
// fiction vs BranchRelaxation / FixupHwLoops).
//
//===----------------------------------------------------------------------===//

#include "HaydnMCCodeEmitter.h"
#include "HaydnFormatERecords.h"
#include "TargetInfo/HaydnTargetInfo.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnFixupKinds.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <functional>
#include <optional>
#include <atomic>
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

// Forward decls for Format E placement (defined with encodeSlotSubInst).
static std::string formatELogicalName(StringRef Name);
static const FormatEMemberRec *
findFormatEMember(StringRef Logical, uint8_t Mode, uint8_t EntryIdx,
                  uint32_t UsedUnitMask = 0);
static bool isFormatENopOpcode(unsigned Opc, const MCInstrInfo &MII);
/// Rebuild \p In as one Format E parcel with golden entry assignment.
/// Prefer sequential as-is serialize for committed BUNDLE_E96_* rows; residual
/// bare logicals place within one row only (no E2↔E3 upgrade on committed
/// composites). Returns false if no injective one-parcel assignment exists
/// (caller must fail closed — never multi-parcel).
static bool buildFormatEPlacedComposite(const MCInst &In,
                                        const MCInstrInfo &MII, MCInst &Out,
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

  // (Stage 1): AIE-style scaled-immediate encoder for the 48-bit WIDE
  // formats. Bound to the `*_wide` / `*_dr` / `hwloop_off*` operand classes
  // via tablegen `EncoderMethod`.
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

// Determine the appropriate fixup kind for an expression operand based on
// the parent instruction opcode. Geometry per HaydnRelocLayout.
// Slot-variant / Format E member opcodes (post-setDesc or encode Wire fill)
// route symbolic operands through getMachineOpValue — peel to logical name
// so LUI_E2_E0_ALU0_I12 gets HI12 (not default FIXUP_HAYDN_32 that clobbers
// Format E indicator → linked objdump <unknown>).
// OpNo is required for multi-field SET_HWLOOP Off1/Off2 (two distinct kinds
// on one instruction); pass ~0u when the operand index is unknown.
static unsigned getExprFixupKind(const MCInst &MI, const MCInstrInfo &MII,
                                 unsigned OpNo = ~0u) {
  const std::string LogicalStorage =
      formatELogicalName(MII.getName(MI.getOpcode()));
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

  switch (MI.getOpcode()) {
  default:
    break;
  case Haydn::LUI:
  case Haydn::LUI_S0:
    // LUI_S0 carries a 12-bit high field (HaydnFU_ALU32_S0_I12).
    // HI12 pairs with LO20 on ADDI32 (not the retired 32-bit-parcel HI20/LO16).
    return Haydn::FIXUP_HAYDN_HI12;
  // ADDI32 RI20: imm20 at s0 bits[37:18] → LO20 (not legacy LO16).
  // ADDI32_W / ADDI32_W_S0 handled below with ORI32_W (block).
  case Haydn::ADDI32:
  case Haydn::ADDI32_S0:
  case Haydn::ADDI32_S1:
  case Haydn::ADDI32_S2:
    return Haydn::FIXUP_HAYDN_LO20;
  // ADDI32S/SUBI* still use signed imm fields; ANDI/ORI/XORI are RI20 ZEXT
  // (ISA: uimm20) — same LO20 window as ADDI32 peers (not LO16).
  case Haydn::ADDI32S:
  case Haydn::SUBI32:
  case Haydn::SUBI32S:
  case Haydn::ADDI32S_S0:
  case Haydn::ADDI32S_S1:
  case Haydn::ADDI32S_S2:
  case Haydn::SUBI32_S0:
  case Haydn::SUBI32_S1:
  case Haydn::SUBI32_S2:
  case Haydn::SUBI32S_S0:
  case Haydn::SUBI32S_S1:
  case Haydn::SUBI32S_S2:
    return Haydn::FIXUP_HAYDN_LO16;
  case Haydn::ANDI32:
  case Haydn::ORI32:
  case Haydn::XORI32:
  case Haydn::ANDI32_S0:
  case Haydn::ANDI32_S1:
  case Haydn::ANDI32_S2:
  case Haydn::ORI32_S0:
  case Haydn::ORI32_S1:
  case Haydn::ORI32_S2:
  case Haydn::XORI32_S0:
  case Haydn::XORI32_S1:
  case Haydn::XORI32_S2:
    return Haydn::FIXUP_HAYDN_LO20;
  // JAL/JALR `_S0` route symbolic call targets through
  // `getMachineOpValue` (not `getCallTargetOpValue`), so this switch decides
  // their kind. Match `getCallFixupKind`'s legacy mapping.
  case Haydn::JAL_S0:
    return Haydn::FIXUP_HAYDN_CallSImm20;
  case Haydn::JALR_S0:
    return Haydn::FIXUP_HAYDN_BranchSImm16;
  // Format E branch `_S0` forms use the same s0 windows as the `_W_S0`
  // peers (cutover). FIXUP_HAYDN_BranchSImm16 still has legacy-parcel
  // geometry (FieldLsb=0, FieldSize=16) and does not patch s0
  // imm12 — linked BEQ/BNE kept offset 0. Map to the WIDE fixup kinds that
  // already carry correct FieldLsb (RI12 → bits[19:8]/8; I12 → bits[15:4]/4).
  // Bare logical opcodes (asm) + private _S0 encode peers.
  case Haydn::BEQ:
  case Haydn::BNE:
  case Haydn::BGE:
  case Haydn::BGEU:
  case Haydn::BLT:
  case Haydn::BLTU:
  case Haydn::BEQ_S0:
  case Haydn::BNE_S0:
  case Haydn::BGE_S0:
  case Haydn::BGEU_S0:
  case Haydn::BLT_S0:
  case Haydn::BLTU_S0:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  case Haydn::BEQZ:
  case Haydn::BNEZ:
  case Haydn::BLTZ:
  case Haydn::BGEZ:
  case Haydn::BEQZ_S0:
  case Haydn::BNEZ_S0:
  case Haydn::BLTZ_S0:
  case Haydn::BGEZ_S0:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12;
  // legacy BEQZ_W/BNEZ_W/BGEZ_W/BLTZ_W — CodeGen emits these opcodes
  // (HaydnConditionOptimizer, HaydnAsmPrinter B/RET expansion, ISel
  // G_BRINDIRECT). Format E encode maps them via golden members; symbolic
  // targets use FIXUP_HAYDN_WIDE_BranchSImm12 (E96 FieldLsb for I12).
  case Haydn::BEQZ_W:
  case Haydn::BNEZ_W:
  case Haydn::BGEZ_W:
  case Haydn::BLTZ_W:
  case Haydn::BEQZ_W_S0:
  case Haydn::BNEZ_W_S0:
  case Haydn::BGEZ_W_S0:
  case Haydn::BLTZ_W_S0:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12;
  // Two-register WIDE cond (RI12): imm12 at s0 bits[19:8] → FieldLsb=8.
  case Haydn::BEQ_W:
  case Haydn::BNE_W:
  case Haydn::BGE_W:
  case Haydn::BGEU_W:
  case Haydn::BLT_W:
  case Haydn::BLTU_W:
  case Haydn::BEQ_W_S0:
  case Haydn::BNE_W_S0:
  case Haydn::BGE_W_S0:
  case Haydn::BGEU_W_S0:
  case Haydn::BLT_W_S0:
  case Haydn::BLTU_W_S0:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  // ADDI32_W/ORI32_W `_S0` carry the wide-reloc operand
  // (simm20_wide_abs / uimm20_wide_abs) — the symbolic operand MUST map to
  // FIXUP_HAYDN_LO20 (the 20-bit absolute LO20 reloc, paired with LUI's HI12).
  // Without this, the default FIXUP_HAYDN_32 clobbers the opcode bytes.
  // The legacy ADDI32_W/ORI32_W are also handled here (the AsmParser matches
  // the bare mnemonic to the logical opcode; residual encode materializes the
  // _S0 member via alts — but getExprFixupKind sees the original opcode).
  case Haydn::ADDI32_W:
  case Haydn::ORI32_W:
  case Haydn::ADDI32_W_S0:
  case Haydn::ORI32_W_S0:
    return Haydn::FIXUP_HAYDN_LO20;
  // (DEFERRED): LD32/ST32/LD64/ST64 still map to FIXUP_HAYDN_LO20.
  // The RISK-5 encoder-side change (all LS -> FIXUP_HAYDN_LS_IMM) was OVER-BROAD
  // it broke the WIDE LSOff20 path (LD32 with a 20-bit offset is correctly
  // LO20, as fixup-selection-regression.s verifies) AND HaydnELFObjectWriter
  // aborts (LS_IMM has no ELF reloc; marked it MC-only). 's real scope
  // is only the NARROW imm6 LS ops (RI6 form, 6-bit) conflated with LO20; the
  // WIDE LSOff20 (20-bit) is correctly LO20. Reverted until the narrow-vs-wide
  // distinction + R_HAYDN_LS_IMM ELF reloc land. LS_IMM kind/geometry
  // stay defined for that follow-up.
  case Haydn::LD32:
  case Haydn::ST32:
  case Haydn::LD64:
  case Haydn::ST64:
  case Haydn::LD32_S0:
  case Haydn::ST32_S0:
  case Haydn::LD64_S0:
  case Haydn::ST64_S0:
    return Haydn::FIXUP_HAYDN_LO20;
  }
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

    if (buildFormatEPlacedComposite(MI, MII, Placed, PlaceStorage)) {
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
    // owns the exact one-parcel commit; MC only serializes.
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
// encodeSlotSubInst — residual composite slot slice (generated emitter)
//===----------------------------------------------------------------------===//
//
// AIEBaseMCCodeEmitter.cpp:134-162 encodes SubInst Desc as-is;
// SubInstFormat/slot geometry come from SubInst.getOpcode().
//
// CodeGen path: post-RA setDesc already committed format members
// (AIEMachineScheduler.cpp:1126-1132 materializeMultiOpcodeInstrs) → encode
// Desc as-is (AIE getSlotKind post-commit, AIEBaseMCFormats.cpp:66-75).
//
// Same TU anonymous namespace as the emitter class (C++ merges them).
namespace {

// Residual hand-asm: matcher may still match the logical public mnemonic
// (ADD32 before ADD32_S*). Materialize that residual via sparse
// getAlternateInstsOpcode[SlotIdx] — the same PlacementAlternative / setDesc
// member table. Local copy only; no MCFlags.
static bool isFormatENopOpcode(unsigned Opc, const MCInstrInfo &MII) {
  if (Opc == Haydn::NOP || Opc == Haydn::NOP_S0)
    return true;
  std::string Log = formatELogicalName(MII.getName(Opc));
  // Empty catalog name is not a product real (unknown pseudo / meta).
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

//===----------------------------------------------------------------------===//
// Golden placement feasibility oracle (solver-side entry/unit law)
//===----------------------------------------------------------------------===//
//
// The packing solver works in the residual S0/S1/S2 slot space; slots are
// abstract labels, not golden entries. A slot-legal cycle can still have no
// (entry, unit) assignment under the golden catalog — two stores are both
// LOADSTORE0-only (an entry-menu collision), and a third X2MUL32 has no free
// MAC unit (a unit collision). Serialization fail-closes on such cycles
// (attemptMode DFS above), so the solver must refuse them up front or the
// compiler dies at emit ("one-parcel placement failed").
//
// haydnFormatEPlacementFeasible answers, per row mode, whether a system of
// distinct representatives exists: distinct entries, distinct units, each
// member drawn from its golden (mode, entry) unit menu. Menus are built once
// per process from FormatEMembers via the same formatELogicalName
// normalization the encode placement uses, so solver accepts and
// serialization successes stay the same set.

static_assert(llvm::haydn::format_e::FormatEUnitCount <= 8,
              "unit menus are uint8_t bitmasks");

// Per-logical golden placement menus (unit bitmask per mode/entry).
struct FormatEPlacementMenus {
  uint8_t E2Entry[2] = {0, 0};
  uint8_t E3Entry[3] = {0, 0, 0};
  bool Real = false;
};

// Standalone MCInstrInfo for opcode-name lookup on paths that carry no MII
// (post-RA HR / SMS / Bundle solver probes). Never latches a failed lookup:
// registration (LLVMInitializeHaydnTargetMC) may run after the first probe
// in unit-test processes.
static const MCInstrInfo *formatEPlacementMII() {
  static std::atomic<const MCInstrInfo *> Cached{nullptr};
  if (const MCInstrInfo *II = Cached.load(std::memory_order_acquire))
    return II;
  const MCInstrInfo *Fresh = getTheHaydnTarget().createMCInstrInfo();
  if (!Fresh)
    return nullptr;
  const MCInstrInfo *Expected = nullptr;
  if (!Cached.compare_exchange_strong(Expected, Fresh,
                                      std::memory_order_acq_rel))
    delete Fresh;
  return Cached.load(std::memory_order_acquire);
}

static ArrayRef<FormatEPlacementMenus>
formatEPlacementTable(const MCInstrInfo &MII) {
  static const std::vector<FormatEPlacementMenus> Table = [&MII] {
    using namespace llvm::haydn::format_e;
    std::vector<FormatEPlacementMenus> T(MII.getNumOpcodes());
    for (unsigned Opc = 0, E = MII.getNumOpcodes(); Opc != E; ++Opc) {
      // Multi-slot pseudos (_MSP) carry their base opcode's placement demand
      // through the solver's rematch paths; they expand before serialization,
      // so emit-side formatELogicalName never needs (and does not get) this
      // peel.
      StringRef Name = MII.getName(Opc);
      Name.consume_back("_MSP");
      const std::string Log = formatELogicalName(Name);
      if (Log.empty() || StringRef(Log).equals_insensitive("NOP"))
        continue;
      const FormatEAltSpan *Span = findAltSpan(Log.c_str());
      if (!Span)
        continue;
      FormatEPlacementMenus M;
      M.Real = true;
      for (unsigned I = 0; I < Span->Count; ++I) {
        const FormatEMemberRec &R =
            FormatEMembers[FormatEAltMemberIds[Span->Begin + I]];
        if (R.IsNop || R.Unit >= 8)
          continue;
        if (R.Mode == 0 && R.EntryIdx < 2)
          M.E2Entry[R.EntryIdx] |= uint8_t(1u << R.Unit);
        else if (R.Mode == 1 && R.EntryIdx < 3)
          M.E3Entry[R.EntryIdx] |= uint8_t(1u << R.Unit);
      }
      T[Opc] = M;
    }
    return T;
  }();
  return Table;
}

// Distinct-entry / distinct-unit assignment search (<=3 members; mirrors the
// attemptMode DFS below). Members without golden menus impose no demand here
// — serialization fail-closes on them independently.
static bool formatEPlacementSDR(ArrayRef<const FormatEPlacementMenus *> Ms,
                                bool E3, unsigned Idx, uint8_t UsedEntries,
                                uint8_t UsedUnits) {
  if (Idx == Ms.size())
    return true;
  if (!Ms[Idx])
    return formatEPlacementSDR(Ms, E3, Idx + 1, UsedEntries, UsedUnits);
  const uint8_t *PerEntry = E3 ? Ms[Idx]->E3Entry : Ms[Idx]->E2Entry;
  const unsigned EntryCount = E3 ? 3 : 2;
  for (unsigned Ent = 0; Ent < EntryCount; ++Ent) {
    if (UsedEntries & (1u << Ent))
      continue;
    unsigned Avail = PerEntry[Ent] & ~unsigned(UsedUnits);
    while (Avail) {
      const unsigned U = Avail & -Avail;
      Avail &= Avail - 1;
      if (formatEPlacementSDR(Ms, E3, Idx + 1,
                              uint8_t(UsedEntries | (1u << Ent)),
                              uint8_t(UsedUnits | U)))
        return true;
    }
  }
  return false;
}

/// Serialize-as-is only when every real is already a committed Format E
/// member for its (mode, entry). That is the post-RA setDesc product shape.
/// Bare logical / residual `_S*` hand-asm falls through to DFS placement.
static bool trySerializeFormatECompositeAsIs(const MCInst &In,
                                             const MCInstrInfo &MII,
                                             MCInst &Out,
                                             SmallVectorImpl<MCInst> &Storage) {
  const bool IsE3 = In.getOpcode() == Haydn::BUNDLE_E96_THREE_ENTRY;
  const bool IsE2 = In.getOpcode() == Haydn::BUNDLE_E96_TWO_ENTRY;
  if (!IsE2 && !IsE3)
    return false;
  const uint8_t Mode = IsE3 ? 1 : 0;
  const unsigned EntryCount = IsE3 ? 3u : 2u;
  if (In.getNumOperands() < EntryCount)
    return false;

  // FormatEMemberOpcodes is defined later in this TU; reverse-map by scanning
  // FormatEMembers + opcode table via linear match on SubInst opcode names
  // that contain _E2_/_E3_ placement markers (committed private members).
  auto isCommittedMemberAt = [&](unsigned Opc, unsigned Entry) -> bool {
    StringRef Name = MII.getName(Opc);
    if (!Name.contains(Mode ? "_E3_" : "_E2_"))
      return false;
    // Entry marker: _E0_ / _E1_ / _E2_ after mode (E3 entry2 is _E2_ after _E3_).
    std::string EntTag = "_E" + std::to_string(Entry) + "_";
    // Avoid matching mode tag: require the entry tag after the mode tag.
    size_t ModePos = Name.find(Mode ? "_E3_" : "_E2_");
    if (ModePos == StringRef::npos)
      return false;
    return Name.find(EntTag, ModePos + 4) != StringRef::npos;
  };

  SmallVector<const MCInst *, 3> AtEntry(EntryCount, nullptr);
  unsigned RealCount = 0;
  for (unsigned E = 0; E < EntryCount; ++E) {
    const MCOperand &Op = In.getOperand(E);
    if (!Op.isInst() || !Op.getInst())
      return false;
    const MCInst *Child = Op.getInst();
    if (isFormatENopOpcode(Child->getOpcode(), MII)) {
      AtEntry[E] = nullptr;
      continue;
    }
    // Residual bare logical / `_S*` — not a durable member for this entry.
    if (!isCommittedMemberAt(Child->getOpcode(), E))
      return false;
    AtEntry[E] = Child;
    ++RealCount;
  }
  if (RealCount == 0)
    return false;

  unsigned NopSlots = EntryCount - RealCount;
  Storage.clear();
  Storage.reserve(NopSlots);
  Out.clear();
  Out.setOpcode(In.getOpcode());
  for (unsigned E = 0; E < EntryCount; ++E) {
    if (AtEntry[E]) {
      Out.addOperand(MCOperand::createInst(AtEntry[E]));
    } else {
      Storage.emplace_back();
      Storage.back().setOpcode(Haydn::NOP);
      Out.addOperand(MCOperand::createInst(&Storage.back()));
    }
  }
  return true;
}

/// Greedy+backtrack assign of real children onto Format E entries with
/// unit injectivity. Prefer sequential as-is serialize for committed
/// BUNDLE_E96_* rows; residual bare logicals may place within one row only.
/// Never multi-parcel and never upgrade E2↔E3 on a committed composite.
static bool buildFormatEPlacedComposite(const MCInst &In,
                                        const MCInstrInfo &MII, MCInst &Out,
                                        SmallVectorImpl<MCInst> &Storage) {
  // Product path: post-RA / AsmPrinter already ordered entries. Serialize
  // that layout without collapsing underfill or swapping rows.
  if (trySerializeFormatECompositeAsIs(In, MII, Out, Storage))
    return true;

  auto looksCommittedMember = [&](unsigned Opc) -> bool {
    StringRef Name = MII.getName(Opc);
    return Name.contains("_E2_") || Name.contains("_E3_");
  };
  for (unsigned I = 0, E = In.getNumOperands(); I != E; ++I) {
    const MCOperand &Op = In.getOperand(I);
    if (!Op.isInst() || !Op.getInst())
      continue;
    if (isFormatENopOpcode(Op.getInst()->getOpcode(), MII))
      continue;
    if (looksCommittedMember(Op.getInst()->getOpcode()))
      return false;
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

  // Assignment state for one mode attempt.
  struct ModeTry {
    uint8_t Mode = 0;
    unsigned EntryCount = 2;
    SmallVector<int, 3> EntryOfKid; // kid -> entry (-1 free)
    SmallVector<const FormatEMemberRec *, 3> MemOfKid;
    uint32_t UsedUnits = 0;
    uint8_t UsedEntries = 0;
  };

  auto attemptMode = [&](uint8_t Mode) -> std::optional<ModeTry> {
    ModeTry T;
    T.Mode = Mode;
    T.EntryCount = Mode ? 3u : 2u;
    if (Reals.size() > T.EntryCount)
      return std::nullopt;
    T.EntryOfKid.assign(Reals.size(), -1);
    T.MemOfKid.assign(Reals.size(), nullptr);

    std::function<bool(unsigned)> dfs = [&](unsigned Kid) -> bool {
      if (Kid == Reals.size())
        return true;
      StringRef Log = LogicalNames[Kid];
      for (uint8_t Entry = 0; Entry < T.EntryCount; ++Entry) {
        if (T.UsedEntries & (1u << Entry))
          continue;
        // Try every legal unit at this entry (not only lowest UnitMap).
        SmallVector<const FormatEMemberRec *, 4> Cands;
        for (unsigned I = 0; I < FormatEMemberCount; ++I) {
          const FormatEMemberRec &M = FormatEMembers[I];
          if (M.IsNop || M.Mode != Mode || M.EntryIdx != Entry)
            continue;
          if (!Log.equals_insensitive(M.Logical))
            continue;
          if (M.Unit < 32 && (T.UsedUnits & (1u << M.Unit)))
            continue;
          Cands.push_back(&M);
        }
        // Deterministic: lower UnitMap first.
        llvm::sort(Cands, [](const FormatEMemberRec *A,
                             const FormatEMemberRec *B) {
          return A->UnitMap < B->UnitMap;
        });
        for (const FormatEMemberRec *Mem : Cands) {
          T.EntryOfKid[Kid] = static_cast<int>(Entry);
          T.MemOfKid[Kid] = Mem;
          T.UsedEntries |= static_cast<uint8_t>(1u << Entry);
          T.UsedUnits |= (1u << Mem->Unit);
          if (dfs(Kid + 1))
            return true;
          T.UsedUnits &= ~(1u << Mem->Unit);
          T.UsedEntries &= static_cast<uint8_t>(~(1u << Entry));
          T.EntryOfKid[Kid] = -1;
          T.MemOfKid[Kid] = nullptr;
        }
      }
      return false;
    };

    if (!dfs(0))
      return std::nullopt;
    return T;
  };

  // Residual bare/_S* only reaches here (committed members already returned).
  // Prefer the composite's row; residual hand-asm may try the other one-parcel
  // row after preferred fails — still never multi-parcel. Committed private
  // members never enter this DFS path.
  const bool PrefersE3 = In.getOpcode() == Haydn::BUNDLE_E96_THREE_ENTRY;
  const bool PrefersE2 = In.getOpcode() == Haydn::BUNDLE_E96_TWO_ENTRY;
  std::optional<ModeTry> Best;
  if (PrefersE3) {
    Best = attemptMode(/*Mode=*/1);
    if (!Best && Reals.size() <= 2)
      Best = attemptMode(/*Mode=*/0);
  } else if (PrefersE2) {
    Best = attemptMode(/*Mode=*/0);
    if (!Best && Reals.size() <= 3)
      Best = attemptMode(/*Mode=*/1);
  } else {
    if (Reals.size() <= 2)
      Best = attemptMode(/*Mode=*/0);
    if (!Best && Reals.size() <= 3)
      Best = attemptMode(/*Mode=*/1);
  }
  if (!Best)
    return false;

  const unsigned EntryCount = Best->EntryCount;
  Out.clear();
  Out.setOpcode(Best->Mode ? Haydn::BUNDLE_E96_THREE_ENTRY
                           : Haydn::BUNDLE_E96_TWO_ENTRY);

  // Map entry -> kid index.
  SmallVector<int, 3> KidAtEntry(EntryCount, -1);
  for (unsigned K = 0, KE = Reals.size(); K != KE; ++K)
    KidAtEntry[Best->EntryOfKid[K]] = static_cast<int>(K);

  // Pre-size NOP storage so emplace cannot reallocate and invalidate
  // MCOperand::createInst pointers into Storage.
  unsigned NopSlots = 0;
  for (unsigned E = 0; E < EntryCount; ++E)
    if (KidAtEntry[E] < 0)
      ++NopSlots;
  Storage.clear();
  Storage.reserve(NopSlots);
  for (unsigned E = 0; E < EntryCount; ++E) {
    if (KidAtEntry[E] >= 0) {
      Out.addOperand(MCOperand::createInst(Reals[KidAtEntry[E]]));
    } else {
      Storage.emplace_back();
      Storage.back().setOpcode(Haydn::NOP);
      Out.addOperand(MCOperand::createInst(&Storage.back()));
    }
  }
  return true;
}

// Strip residual slot member / wide / LS suffixes to recover the logical
// catalog name used by Format E records.
static std::string formatELogicalName(StringRef Name) {
  StringRef Base = Name;
  // Peel known residual suffixes (order matters for compound tails).
  auto peel = [&](StringRef Suf) {
    if (Base.ends_with(Suf))
      Base = Base.drop_back(Suf.size());
  };
  // Format E live member tails: LOGICAL_E2_E0_UNIT_TYPE / _E3_E1_…
  for (StringRef Marker : {"_E2_", "_E3_"}) {
    size_t Idx = Base.find(Marker);
    if (Idx != StringRef::npos) {
      Base = Base.take_front(Idx);
      break;
    }
  }
  // Residual slot / member tails.
  for (int Pass = 0; Pass < 3; ++Pass) {
    StringRef Before = Base;
    for (StringRef Suf :
         {"_S0", "_S1", "_S2", "_LD_S0", "_LD_S1", "_LD_S2", "_M0S0LS",
          "_M0S1LS", "_M0S2LS", "_M1S0LS", "_M1S1LS", "_M1S2LS"})
      peel(Suf);
    if (Base == Before)
      break;
  }
  // Format E golden catalogs AR pre-load as PLDWWUA_POST (no bare PLDWWUA
  // member). Selector emits logical PLDWWUA — map for placement/encode.
  if (Base.equals_insensitive("PLDWWUA"))
    Base = "PLDWWUA_POST";
  // Wide demoted forms: BEQ_W → BEQ, ADDI32_W → ADDI32, SET_HWLOOP_F2_W →
  // SET_HWLOOP_F2 (keep the _F2 catalog logical — HWLRIIR). Dropping "_F2_W"
  // as a unit used to collapse F2 into SET_HWLOOP (HWLRIII), so the register
  // count was packed as a 16-bit imm and the parcel decoded as NOP — memcpy
  // HWLoops ran the body once and returned garbage.
  if (Base.ends_with("_F2_W"))
    Base = Base.drop_back(2); // …_F2_W → …_F2
  else if (Base.ends_with("_W"))
    Base = Base.drop_back(2);
  // Do NOT peel a bare "_F2": SET_HWLOOP_F2 is a distinct golden logical.

  // Public mnemonic → Format E golden catalog logical (freestanding path).
  // Residual legacy public names are not golden catalog strings; without
  // this map encode fails golden placement (was residual truncate → <unknown>).
  if (Base.equals_insensitive("LD32") || Base.equals_insensitive("LW") ||
      Base.equals_insensitive("LD32_REG"))
    return Base.equals_insensitive("LD32_REG") ? "S_LW_WITH_REG"
                                               : "S_LW_WITH_IMM";
  if (Base.equals_insensitive("ST32") || Base.equals_insensitive("SW") ||
      Base.equals_insensitive("ST32_REG"))
    return Base.equals_insensitive("ST32_REG") ? "S_SW_WITH_REG"
                                               : "S_SW_WITH_IMM";
  if (Base.equals_insensitive("LD64") || Base.equals_insensitive("LD64_REG"))
    return Base.equals_insensitive("LD64_REG") ? "D_LDW_WITH_REG"
                                               : "D_LDW_WITH_IMM";
  // ST64 must be D_SDW (full DR[63:0] → mem64, scale=3). D_SW_L only stores
  // DR[31:0] as mem32 — mapping ST64 there dropped the high half and applied
  // word scale (<<2) instead of dword (<<3), breaking 64-bit stores / udivdi3
  // spill traffic / CoreMark-adjacent long long paths.
  if (Base.equals_insensitive("ST64") || Base.equals_insensitive("ST64_REG"))
    return Base.equals_insensitive("ST64_REG") ? "D_SDW_WITH_REG"
                                               : "D_SDW_WITH_IMM";
  if (Base.equals_insensitive("LD8") || Base.equals_insensitive("LB") ||
      Base.equals_insensitive("LD8_REG"))
    return Base.ends_with_insensitive("REG") ? "S_LBS_WITH_REG"
                                             : "S_LBS_WITH_IMM";
  if (Base.equals_insensitive("LDU8") || Base.equals_insensitive("LBU") ||
      Base.equals_insensitive("LDU8_REG"))
    return Base.ends_with_insensitive("REG") ? "S_LBU_WITH_REG"
                                             : "S_LBU_WITH_IMM";
  if (Base.equals_insensitive("ST8") || Base.equals_insensitive("SB") ||
      Base.equals_insensitive("ST8_REG"))
    return Base.ends_with_insensitive("REG") ? "S_SB_WITH_REG"
                                             : "S_SB_WITH_IMM";
  if (Base.equals_insensitive("LD16") || Base.equals_insensitive("LH") ||
      Base.equals_insensitive("LHWS") || Base.equals_insensitive("LD16_REG"))
    return Base.ends_with_insensitive("REG") ? "S_LHWS_WITH_REG"
                                             : "S_LHWS_WITH_IMM";
  if (Base.equals_insensitive("LDU16") || Base.equals_insensitive("LHU") ||
      Base.equals_insensitive("LHWU") || Base.equals_insensitive("LDU16_REG"))
    return Base.ends_with_insensitive("REG") ? "S_LHWU_WITH_REG"
                                             : "S_LHWU_WITH_IMM";
  if (Base.equals_insensitive("ST16") || Base.equals_insensitive("SH") ||
      Base.equals_insensitive("SHW") || Base.equals_insensitive("ST16_REG"))
    return Base.ends_with_insensitive("REG") ? "S_SHW_WITH_REG"
                                             : "S_SHW_WITH_IMM";
  // POST/PRE public forms (imm).
  if (Base.equals_insensitive("LD32_POST") ||
      Base.equals_insensitive("LD32_POST_INC"))
    return "S_LW_POST_IMM";
  if (Base.equals_insensitive("ST32_POST") ||
      Base.equals_insensitive("ST32_POST_INC"))
    return "S_SW_POST_IMM";
  if (Base.equals_insensitive("LD32_PRE") ||
      Base.equals_insensitive("LD32_PRE_INC"))
    return "S_LW_PRE_IMM";
  if (Base.equals_insensitive("ST32_PRE") ||
      Base.equals_insensitive("ST32_PRE_INC"))
    return "S_SW_PRE_IMM";
  if (Base.equals_insensitive("LD64_POST"))
    return "D_LDW_POST_IMM";
  if (Base.equals_insensitive("ST64_POST"))
    return "D_SDW_POST_IMM";
  // Legacy codegen public names → golden catalog.
  if (Base.equals_insensitive("SEXT_GPR32_TO_DR64") ||
      Base.equals_insensitive("SEXT32T64"))
    return "SEXT32T64";
  if (Base.equals_insensitive("MOV_GPR_TO_DR64") ||
      Base.equals_insensitive("MOVE_GPR_TO_DR64") ||
      Base.equals_insensitive("ZEXT_GPR32_TO_DR64"))
    return "SEXT32T64";
  if (Base.equals_insensitive("RET"))
    return "JALR";

  return Base.str();
}

// Entry field packing is TableGen Inst{} on live Format E members
// (HaydnFormatsE96Members.td.inc). encodeSlotSubInst fills a member MCInst
// and calls getBinaryCodeForInstr — do not reintroduce hand field packers.

} // end anonymous namespace (Format E placement helpers)

std::pair<bool, bool>
llvm::haydnFormatEPlacementFeasible(ArrayRef<unsigned> LogicalOpcodes) {
  if (LogicalOpcodes.empty())
    return {true, true};
  const MCInstrInfo *MII = formatEPlacementMII();
  if (!MII)
    return {true, true}; // MC target not registered — no refinement possible
  const ArrayRef<FormatEPlacementMenus> Table = formatEPlacementTable(*MII);
  SmallVector<const FormatEPlacementMenus *, 3> Ms;
  for (unsigned Opc : LogicalOpcodes) {
    const FormatEPlacementMenus *M =
        Opc < Table.size() && Table[Opc].Real ? &Table[Opc] : nullptr;
    Ms.push_back(M);
  }
  const bool E2 =
      LogicalOpcodes.size() <= 2 &&
      formatEPlacementSDR(Ms, /*E3=*/false, 0, /*UsedEntries=*/0,
                          /*UsedUnits=*/0);
  const bool E3 =
      LogicalOpcodes.size() <= 3 &&
      formatEPlacementSDR(Ms, /*E3=*/true, 0, /*UsedEntries=*/0,
                          /*UsedUnits=*/0);
  return {E2, E3};
}

bool llvm::haydnFormatEHasGoldenPlacement(unsigned Opcode) {
  const MCInstrInfo *MII = formatEPlacementMII();
  if (!MII)
    return false;
  const ArrayRef<FormatEPlacementMenus> Table = formatEPlacementTable(*MII);
  return Opcode < Table.size() && Table[Opcode].Real;
}

// MemberId → Haydn::<E96 member opcode> (generated with live TD members).
#define GET_FORMAT_E_MEMBER_OPCODES
#include "HaydnGenFormatEMemberOpcodes.inc"

/// Map logical / residual `_S*` MC operands onto a live Format E member Inst
/// (wire field order + reg classes from tblgen Desc). Used only to feed
/// getBinaryCodeForInstr — bit placement is TableGen Inst{}.
static bool fillFormatEMemberInst(const FormatEMemberRec &Mem,
                                  const MCInst &Logical, const MCInstrInfo &MII,
                                  const MCRegisterInfo &MRI, MCInst &Out) {
  if (Mem.MemberId >= FormatEMemberOpcodeCount)
    return false;
  const unsigned MemberOpc = FormatEMemberOpcodes[Mem.MemberId];
  if (MemberOpc == 0)
    return false;

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
  // SET_HWLOOP / SET_HWLOOP_F2: logical order is sel, off1, off2, cnt|rs.
  // Bag-sort (all imms then all exprs) puts cnt imm into the Off1 slot when
  // Off1/Off2 are symbolic (Imms=[sel,cnt], Exprs=[start,end]) — Off fields
  // swap/clobber and typed HWLoopOff range-checks fail. Preserve order.
  if (Log.equals_insensitive("SET_HWLOOP") ||
      Log.equals_insensitive("SET_HWLOOP_F2") ||
      Log.equals_insensitive("SET_HWLOOP_W") ||
      Log.equals_insensitive("SET_HWLOOP_F2_W")) {
    const unsigned MemberOpc = FormatEMemberOpcodes[Mem.MemberId];
    const MCInstrDesc &Desc = MII.get(MemberOpc);
    if (Logical.getNumOperands() < Desc.getNumOperands())
      return false;
    Out.clear();
    Out.setOpcode(MemberOpc);
    for (unsigned OI = 0, OE = Desc.getNumOperands(); OI != OE; ++OI)
      Out.addOperand(Logical.getOperand(OI));
    return true;
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

  // Product Format E: select golden member, fill wire-shaped MCInst, then
  // **tblgen** getBinaryCodeForInstr (Inst{} from HaydnFormatsE96Members.td.inc).
  // C++ does not pack entry fields. Residual truncate is forbidden.
  (void)Kind;
  (void)EntryLSB;
  if (isFormatENopOpcode(SubInst.getOpcode(), MII)) {
    Op = APInt(EntryWidth, 0); // zero entry → NOP under Format E inverse
    return;
  }
  std::string Logical = formatELogicalName(MII.getName(SubInst.getOpcode()));
  const uint8_t Mode = IsFormatE3 ? 1 : 0;
  // Claim units already chosen for lower entry indices (matches
  // buildFormatEPlacedComposite unit injectivity).
  uint32_t UsedUnits = 0;
  for (unsigned S = 0; S < SlotIdx && S < Composite.getNumOperands(); ++S) {
    const MCOperand &Prev = Composite.getOperand(S);
    if (!Prev.isInst() || !Prev.getInst())
      continue;
    if (isFormatENopOpcode(Prev.getInst()->getOpcode(), MII))
      continue;
    // Prefer exact committed member opcode when present (serialize-as-is).
    const FormatEMemberRec *PM = nullptr;
    for (unsigned I = 0; I < FormatEMemberCount && !PM; ++I) {
      const FormatEMemberRec &M = FormatEMembers[I];
      if (M.IsNop || M.Mode != Mode || M.EntryIdx != S)
        continue;
      if (M.MemberId < FormatEMemberOpcodeCount &&
          FormatEMemberOpcodes[M.MemberId] == Prev.getInst()->getOpcode())
        PM = &M;
    }
    if (!PM) {
      std::string PrevLog =
          formatELogicalName(MII.getName(Prev.getInst()->getOpcode()));
      PM = findFormatEMember(PrevLog, Mode, static_cast<uint8_t>(S), UsedUnits);
    }
    if (PM)
      UsedUnits |= (1u << PM->Unit);
  }
  // Serialize-as-is: when SubInst is already the committed Format E member for
  // this (mode, entry), use that MemberId — do not re-pick lowest UnitMap.
  const FormatEMemberRec *Mem = nullptr;
  for (unsigned I = 0; I < FormatEMemberCount; ++I) {
    const FormatEMemberRec &M = FormatEMembers[I];
    if (M.IsNop || M.Mode != Mode || M.EntryIdx != SlotIdx)
      continue;
    if (M.MemberId < FormatEMemberOpcodeCount &&
        FormatEMemberOpcodes[M.MemberId] == SubInst.getOpcode()) {
      if (M.Unit < 32 && (UsedUnits & (1u << M.Unit)))
        continue;
      Mem = &M;
      break;
    }
  }
  const StringRef SubName = MII.getName(SubInst.getOpcode());
  const bool IsCommittedMemberName =
      SubName.contains("_E2_") || SubName.contains("_E3_");
  if (!Mem && !IsCommittedMemberName)
    Mem = findFormatEMember(Logical, Mode, static_cast<uint8_t>(SlotIdx),
                            UsedUnits);
  if (!Mem || Mem->MemberId >= FormatEMemberOpcodeCount) {
    if (IsCommittedMemberName) {
      report_fatal_error(
          Twine("Haydn MC: committed Format E member '") +
              MII.getName(SubInst.getOpcode()) +
              "' does not match mode=" +
              Twine(static_cast<unsigned>(Mode)) + " entry=" +
              Twine(SlotIdx) + " — refuse logical re-place (serialize-only)",
          /*GenCrashDiag=*/false);
    }
    std::string Msg =
        "Haydn MC: Format E entry encode miss for '" + Logical + "' mode=" +
        std::to_string(static_cast<unsigned>(Mode)) +
        " entry=" + std::to_string(SlotIdx) +
        " — no golden member / opcode table";
    report_fatal_error(Twine(Msg), /*GenCrashDiag=*/false);
  }

  const unsigned MemberOpc = FormatEMemberOpcodes[Mem->MemberId];
  if (MemberOpc == Haydn::NOP || isFormatENopOpcode(MemberOpc, MII)) {
    Op = APInt(EntryWidth, 0);
    return;
  }

  // Build wire-shaped member MCInst from logical/residual operands, then
  // encode solely via TableGen Inst{} (getBinaryCodeForInstr).
  MCInst Wire;
  if (!fillFormatEMemberInst(*Mem, SubInst, MII, *Ctx.getRegisterInfo(), Wire)) {
    report_fatal_error(
        Twine("Haydn MC: failed to fill Format E member operands for '") +
            Logical + "' → " + MII.getName(MemberOpc),
        /*GenCrashDiag=*/false);
  }
  SmallVector<MCFixup, 4> LocalFixups;
  // HaydnGenMCCodeEmitter uses fixed 96-bit Inst/Scratch (Format E parcel).
  // Passing a wider Scratch hits APInt::zext(96) assert (width >= BitWidth).
  APInt Scratch(96, 0);
  APInt InstBits(96, 0);
  getBinaryCodeForInstr(Wire, LocalFixups, InstBits, Scratch, STI);
  Op = InstBits.zextOrTrunc(EntryWidth);
  for (const MCFixup &F : LocalFixups)
    addHaydnFixup(Fixups, F.getOffset(), F.getValue(), F.getKind());
}

//===----------------------------------------------------------------------===//
// Fixup helpers
//===----------------------------------------------------------------------===//

unsigned HaydnMCCodeEmitter::getBranchFixupKind(const MCInst &MI) const {
  // Match getExprFixupKind for the same opcode classes so both
  // getBranchTargetOpValue and getMachineOpValue attach Format E field
  // geometry (WIDE_BranchSImm12 / RI12, not legacy BranchSImm16).
  switch (MI.getOpcode()) {
  case Haydn::BEQ:
  case Haydn::BNE:
  case Haydn::BGE:
  case Haydn::BGEU:
  case Haydn::BLT:
  case Haydn::BLTU:
  case Haydn::BEQ_S0:
  case Haydn::BNE_S0:
  case Haydn::BGE_S0:
  case Haydn::BGEU_S0:
  case Haydn::BLT_S0:
  case Haydn::BLTU_S0:
  case Haydn::BEQ_W:
  case Haydn::BNE_W:
  case Haydn::BGE_W:
  case Haydn::BGEU_W:
  case Haydn::BLT_W:
  case Haydn::BLTU_W:
  case Haydn::BEQ_W_S0:
  case Haydn::BNE_W_S0:
  case Haydn::BGE_W_S0:
  case Haydn::BGEU_W_S0:
  case Haydn::BLT_W_S0:
  case Haydn::BLTU_W_S0:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  case Haydn::BEQZ:
  case Haydn::BNEZ:
  case Haydn::BLTZ:
  case Haydn::BGEZ:
  case Haydn::BEQZ_S0:
  case Haydn::BNEZ_S0:
  case Haydn::BLTZ_S0:
  case Haydn::BGEZ_S0:
  case Haydn::BEQZ_W:
  case Haydn::BNEZ_W:
  case Haydn::BGEZ_W:
  case Haydn::BLTZ_W:
  case Haydn::BEQZ_W_S0:
  case Haydn::BNEZ_W_S0:
  case Haydn::BGEZ_W_S0:
  case Haydn::BLTZ_W_S0:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12;
  default:
    return Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI;
  }
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
                  getExprFixupKind(MI, MII, OpNo));
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

  // D1/§5.14: ALL branches use 2-byte units (<<1). Unified at ÷2.
  (void)Opcode;
  unsigned Alignment = 2;
  unsigned Shift = 1;

  if (MO.isImm()) {
    int64_t Imm = MO.getImm();
    if (Imm & (Alignment - 1)) {
      Ctx.reportError(SMLoc(), "branch offset must be 2-byte aligned");
    }
    Op = static_cast<uint64_t>(Imm >> Shift);
    return;
  }

  if (MO.isExpr()) {
    addHaydnFixup(Fixups, /*Offset=*/0, MO.getExpr(), getBranchFixupKind(MI));
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
      // Linear 20-bit encoding: Inst[19:0] = (offset >> 1) & 0xFFFFF.
      Op = static_cast<uint64_t>((Imm >> 1) & 0xFFFFF);
      return;
    }
    Op = static_cast<uint64_t>(Imm);
    return;
  }

  if (MO.isExpr()) {
    addHaydnFixup(Fixups, /*Offset=*/0, MO.getExpr(), getCallFixupKind(MI));
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
// (Stage 1): AIE-style scaled-immediate encoder for 48-bit WIDE formats.
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
    if (FixupKind != Haydn::FIXUP_HAYDN_NONE) {
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
// and getBranchTargetOpValue). Format E encode applies ValueShift=1 so the
// wire field stores halfwords; dump recovers bytes via <<1. A 2-parcel skip
// is therefore createImm(2 * Parcel) bytes → field Parcel after ÷2.
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
  // Format E encode applies halfword ValueShift (Imm >> 1).
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
