//===-- HaydnAsmBackend.cpp - Haydn Assembler Backend ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnAsmBackend.h"
#include "HaydnFixupKinds.h"
#include "HaydnRelocLayout.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include <optional>

using namespace llvm;

// Format E PC is the 12-byte parcel base. Fixup byte offsets can sit mid-parcel
// when a field lives in entry1/entry2 (EntryLSB/8 = 6+). Default MC P =
// frag_off + fixup_off then fails Align=4 (hwloop) / Align=2 checks with a
// spurious "mis-aligned relocation target". Seed Value with Abs % Parcel so
// the default subtract lands on align_down(Abs, Parcel) (Xtensa l32r pattern).
std::optional<bool> HaydnAsmBackend::evaluateFixup(const MCFragment &F,
                                                   MCFixup &Fixup, MCValue &,
                                                   uint64_t &Value) {
  if (!Fixup.isPCRel() || !Asm)
    return std::nullopt;
  const unsigned Parcel = haydnProductionParcelBytes().Value;
  if (Parcel <= 1)
    return std::nullopt;
  const uint64_t Abs = Asm->getFragmentOffset(F) + Fixup.getOffset();
  Value = Abs % Parcel;
  return std::nullopt;
}

// Check whether the given instruction may need relaxation.
// Format E has no MC-layer opcode relaxation.
bool HaydnAsmBackend::mayNeedRelaxation(unsigned Opcode,
                                        ArrayRef<MCOperand> Operands,
                                        const MCSubtargetInfo &STI) const {
  (void)Opcode;
  (void)Operands;
  (void)STI;
  return false;
}

// mayNeedRelaxation is always false, so this override is never consulted
// for product emission. Keep a trivial override that never requests
// relaxation.
bool HaydnAsmBackend::fixupNeedsRelaxationAdvanced(
    const MCFragment &, const MCFixup &, const MCValue &, uint64_t,
    bool) const {
  return false;
}

// Format E has no MC-layer opcode relaxation.
void HaydnAsmBackend::relaxInstruction(MCInst &Inst,
                                       const MCSubtargetInfo &STI) const {
  (void)Inst;
  (void)STI;
}

// Apply a fixup value to the instruction/data at the given offset.
void HaydnAsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                                   const MCValue &Target, uint8_t *Data,
                                   uint64_t Value, bool IsResolved) {
  MCFixupKind Kind = Fixup.getKind();

  // Record relocations for unresolved fixups
  maybeAddReloc(F, Fixup, Target, Value, IsResolved);

  // If this is a relocation (not a fixup), don't apply it now
  if (mc::isRelocation(Kind))
    return;

  // For fixups that need relocations, record them but don't apply
  if (!IsResolved) {
    // The linker will handle this
    return;
  }

  // === Single-source reloc table (HaydnRelocLayout). Both this MC writer and
  // lld's Haydn::relocate / getImplicitAddend / inBranchRange delegate to the
  // same geometry + transform table (branch/call byte PC+imm, hwloop word ÷4), so
  // reader and writer cannot diverge and no consumer keeps a parallel isInt
  // field-width table.
  //
  // Legacy exception: FIXUP_HAYDN_HWLoopOffset is the 8-byte *placeholder* form
  // whose loop_start/loop_end tag-compensation the geometric table cannot
  // express. Tag compensation stays local; product SET_HWLOOP uses Off1/Off2
  // rows only.
  if (Kind == Haydn::FIXUP_HAYDN_HWLoopOffset) {
    constexpr uint8_t HWLoopTagMask = 0x3;        // tag at bits[17:16] = byte2[1:0]
    constexpr uint64_t HWLoopEndWordCompensation = 4; // word1 sits at PC+4
    constexpr uint8_t HWLoopTagLoopEnd = 0b01;
    uint8_t Tag = 0b00;
    if (Fixup.getOffset() + 3 <= F.getSize())
      Tag = Data[2] & HWLoopTagMask;
    uint64_t Compensated = (Tag == HWLoopTagLoopEnd)
                               ? (Value + HWLoopEndWordCompensation)
                               : Value;
    if (Compensated & 0x3) {
      getContext().reportError(Fixup.getLoc(),
                               "hwloop offset must be 4-byte aligned");
      return;
    }
    int64_t Offset = static_cast<int64_t>(Compensated) >> 2;
    if (!isInt<16>(Offset)) {
      getContext().reportError(Fixup.getLoc(), "hwloop offset out of range");
      return;
    }
    if (Fixup.getOffset() + 4 > F.getSize()) {
      getContext().reportError(Fixup.getLoc(), "fixup offset exceeds fragment size");
      return;
    }
    uint64_t FieldVal =
        static_cast<uint64_t>(static_cast<int16_t>(Offset)) & 0xFFFF;
    HaydnReloc::patchField(Data, FieldVal, 4, 16, 0);
    return;
  }

  HaydnReloc::RelocKind R = HaydnReloc::mapFixupKind(static_cast<unsigned>(Kind));
  if (R != HaydnReloc::RelocKind::Invalid) {
    HaydnReloc::RelocCompute Comp = HaydnReloc::computeRelocValue(R, Value);
    if (!Comp.OK) {
      getContext().reportError(Fixup.getLoc(), Comp.Err);
      return;
    }
    const HaydnReloc::RelocFieldInfo &FI = HaydnReloc::getRelocFieldInfo(R);
    if (Fixup.getOffset() + FI.NBytes > F.getSize()) {
      getContext().reportError(Fixup.getLoc(), "fixup offset exceeds fragment size");
      return;
    }
    // Control P is the parcel origin (AIE translateFixupsInComposite offset 0;
    // AIEBaseMCCodeEmitter.cpp:231-232). A mid-parcel r_offset makes lld
    // compute P = PC+N so linked B/JAL targets miss the record grid.
    if (Asm && FI.IsPCRel) {
      switch (R) {
      case HaydnReloc::RelocKind::BranchSImm16:
      case HaydnReloc::RelocKind::CallSImm20:
      case HaydnReloc::RelocKind::WIDE_BranchSImm12:
      case HaydnReloc::RelocKind::WIDE_BranchSImm12_RI:
      case HaydnReloc::RelocKind::WIDE_CallSImm20:
      case HaydnReloc::RelocKind::HWLoopOff1:
      case HaydnReloc::RelocKind::HWLoopOff2:
      case HaydnReloc::RelocKind::JALRSImm12: {
        const unsigned Parcel = haydnProductionParcelBytes().Value;
        const uint64_t Abs =
            Asm->getFragmentOffset(F) + Fixup.getOffset();
        if (Parcel > 1 && (Abs % Parcel) != 0) {
          getContext().reportError(
              Fixup.getLoc(),
              "control relocation offset is not an exact Format E record");
          return;
        }
        // JALR is rs+imm (not PC-relative); odd immediates are legal.
        // PC-relative B/JAL/HWLOOP displacements must be whole parcels so
        // the resolved target is an exact code record. Align=2/4 failures
        // share computeRelocValue's "mis-aligned relocation target" with
        // the branch path; the parcel-grid check is only for Align-ok
        // displacements that still miss a 12-byte record.
        if (R != HaydnReloc::RelocKind::JALRSImm12 && FI.IsPCRel &&
            Parcel > 1 &&
            (static_cast<int64_t>(Value) % static_cast<int64_t>(Parcel)) !=
                0) {
          if (FI.Align <= 1 ||
              (static_cast<int64_t>(Value) %
               static_cast<int64_t>(FI.Align)) == 0) {
            getContext().reportError(
                Fixup.getLoc(),
                "control relocation target is not an exact Format E record");
            return;
          }
        }
        break;
      }
      default:
        break;
      }
    }
    // Data is pre-adjusted to Fixup.getOffset (lesson): write at Data[0].
    // Table FieldLsb (getFixupKindInfo TargetOffset) is E2 e0 only.
    // applyFixup patches parcel-absolute bits via resolveFieldLsb — do not
    // also shift by TargetOffset (AIE Dummy TargetOffset: AIEBaseAsmBackend.h
    // getFixupKindInfo 56-71; AIE applyFixup shifts only generic FK_Data_*).
    // HI12/LO20/PC_LO20/JALRSImm12 E3 e0/e1/e2 and E2 e1 windows live here.
    const unsigned FieldLsb = HaydnReloc::resolveFieldLsb(R, Data);
    HaydnReloc::patchField(Data, Comp.FieldVal, FI.NBytes, FI.FieldSize, FieldLsb);
    return;
  }

  // Generic data fixups (FK_Data_* / FK_PCRel_*): raw little-endian byte write.
  // Unknown target kinds used to llvm_unreachable, which is abort-not-diagnose
  // and in NDEBUG fell through to the 32-bit data write — the same header
  // clobber as an untyped FIXUP_HAYDN_32 on a Format E record. Fail closed.
  // Peer: AIEBaseAsmBackend.cpp:21-22 llvm_unreachable; Haydn diagnoses.
  if (Kind >= FirstTargetFixupKind) {
    getContext().reportError(Fixup.getLoc(),
                             "unknown Haydn target fixup kind");
    return;
  }
  MCFixupKindInfo Info = MCAsmBackend::getFixupKindInfo(Kind);
  unsigned NumBytes = alignTo(Info.TargetSize + Info.TargetOffset, 8) / 8;
  if (Fixup.getOffset() + NumBytes > F.getSize()) {
    getContext().reportError(Fixup.getLoc(), "fixup offset exceeds fragment size");
    return;
  }
  for (unsigned Idx = 0; Idx != NumBytes; ++Idx)
    Data[Idx] |= static_cast<uint8_t>((Value >> (Idx * 8)) & 0xFF);
}

// Get information about a fixup kind.
MCFixupKindInfo HaydnAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  // Names only — geometry (TargetOffset/TargetSize) is derived from the
  // single-source HaydnRelocLayout table so getFixupKindInfo can never drift
  // from applyFixup (MC) or Haydn::relocate (lld). Order matches the
  // FIXUP_HAYDN_* enum in HaydnFixupKinds.h.
  static const char *const Names[] = {
      "FIXUP_HAYDN_NONE",         "FIXUP_HAYDN_32",
      "FIXUP_HAYDN_SImm16",       "FIXUP_HAYDN_BranchSImm16",
      "FIXUP_HAYDN_CallSImm20",   "FIXUP_HAYDN_HI20",
      "FIXUP_HAYDN_LO16",         "FIXUP_HAYDN_GOT_HI20",
      "FIXUP_HAYDN_TPREL_HI20",   "FIXUP_HAYDN_TPREL_LO16",
      "FIXUP_HAYDN_32_PCREL",     "FIXUP_HAYDN_C_BranchSImm4",
      "FIXUP_HAYDN_C_UImm4",      "FIXUP_HAYDN_C_BranchSImm10",
      "FIXUP_HAYDN_HWLoopOffset", "FIXUP_HAYDN_HWLoopOff1",
      "FIXUP_HAYDN_HWLoopOff2",   "FIXUP_HAYDN_LongBranchSImm20",
      "FIXUP_HAYDN_HI12",         "FIXUP_HAYDN_LO20",
      "FIXUP_HAYDN_PC_LO20",      "FIXUP_HAYDN_WIDE_BranchSImm12",
      "FIXUP_HAYDN_WIDE_BranchSImm12_RI",
      "FIXUP_HAYDN_WIDE_CallSImm20",
      "FIXUP_HAYDN_S0LSOff4_2",   "FIXUP_HAYDN_S0LSOff4_3",
      "FIXUP_HAYDN_S0LSOff2_0",   "FIXUP_HAYDN_S0LSOff3_0",
      "FIXUP_HAYDN_LS_IMM",       "FIXUP_HAYDN_JALRSImm12",
      "FIXUP_HAYDN_CSR_UImm8",
  };
  static_assert(std::size(Names) == Haydn::NumTargetFixupKinds,
                "Names[] must list every target fixup kind, in enum order");

  // Handle generic fixup kinds
  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);

  // Handle relocation fixups (from.reloc directive or assembler)
  if (mc::isRelocation(Kind))
    return {};

  assert(unsigned(Kind - FirstTargetFixupKind) < Haydn::NumTargetFixupKinds &&
         "Invalid fixup kind!");

  HaydnReloc::RelocKind R = HaydnReloc::mapFixupKind(Kind);
  const HaydnReloc::RelocFieldInfo &FI = HaydnReloc::getRelocFieldInfo(R);
  // This LLVM tree stores PC-relativity on MCFixup::isPCRel (set by the
  // encoder), not on MCFixupKindInfo::Flags. Keep Flags=0 (matches RISCV).
  // TargetOffset is the E2 e0 table FieldLsb (intra-parcel for the default
  // row). applyFixup / lld relocate use resolveFieldLsb and must not shift
  // this value a second time — TargetOffset is metadata, not a write shift.
  return MCFixupKindInfo{Names[Kind - FirstTargetFixupKind], FI.FieldLsb,
                         FI.FieldSize, 0};
}

// Write NOP data to the output stream.
std::unique_ptr<MCObjectTargetWriter>
HaydnAsmBackend::createObjectTargetWriter() const {
  return createHaydnELFObjectWriter();
}

bool HaydnAsmBackend::finishLayout() const {
  if (!Asm)
    return false;
  // Consumer identity is production ELFFlagsValue (EF_HAYDN_E96=0x1).
  // Peer: RISCV.cpp:169 intersects object e_flags; AIE.cpp:66-71 copies the
  // first file. Haydn refuses mixed/zero/unknown and never mints a second
  // e_machine (official 259 is Kalray KVX).
  const uint32_t Flags =
      haydn::format::getProductionObjectEncodingProfile().ELFFlagsValue;
  assert(Flags != 0 && "E96 product profile must allocate nonzero e_flags");
  static_cast<ELFObjectWriter &>(Asm->getWriter()).setELFHeaderEFlags(Flags);
  return false;
}

bool HaydnAsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                   const MCSubtargetInfo *) const {
  // Product padding is whole Format E parcels only. Parcel length comes from
  // the production registry EncodedBytes (not a local 12/16 literal). When
  // golden has not registered a canonical idle/completion wire form, refuse
  // to invent all-zero or header-only pad bytes (all-zero is not Format E:
  // indicator must be 111). Partial residuals (non-multiples) are rejected.
  return haydnWriteCanonicalIdlePad(OS, Count);
}

// Create the Haydn assembly backend.
MCAsmBackend *llvm::createHaydnAsmBackend(const Target &T,
                                           const MCSubtargetInfo &STI,
                                           const MCRegisterInfo &MRI,
                                           const MCTargetOptions &Options) {
  return new HaydnAsmBackend(STI, Options);
}
