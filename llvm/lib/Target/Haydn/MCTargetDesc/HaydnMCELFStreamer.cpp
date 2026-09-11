//===- HaydnMCELFStreamer.cpp - Haydn subclass of MCELFStreamer -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HaydnMCELFStreamer.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <numeric>

using namespace llvm;

HaydnMCELFStreamer::HaydnMCELFStreamer(MCContext &Context,
                                       std::unique_ptr<MCAsmBackend> TAB,
                                       std::unique_ptr<MCObjectWriter> OW,
                                       std::unique_ptr<MCCodeEmitter> Emitter)
    : MCELFStreamer(Context, std::move(TAB), std::move(OW),
                    std::move(Emitter)) {}

void HaydnMCELFStreamer::emitSymbolsInInst(const MCInst &Inst) {
  // HexagonMCELFStreamer::EmitSymbol: walk operands for isExpr. Haydn extends
  // that with recursive isInst descent for Format E BUNDLE children (AIE
  // composite / Hexagon packet both nest real ops under a container MI).
  for (unsigned I = 0, E = Inst.getNumOperands(); I != E; ++I) {
    const MCOperand &Op = Inst.getOperand(I);
    if (Op.isExpr())
      visitUsedExpr(*Op.getExpr());
    else if (Op.isInst() && Op.getInst())
      emitSymbolsInInst(*Op.getInst());
  }
}

void HaydnMCELFStreamer::emitIdleParcels(unsigned Count) {
  if (Count == 0)
    return;
  // Full-slot architectural-NOP parcel only. Refuse rather than invent a
  // singleton/underfill or top-pad completion.
  SmallVector<char, 16> Idle;
  if (!haydnTryGetCanonicalIdleParcel(Idle))
    report_fatal_error(
        "Haydn MC: no product-approved idle parcel for executable pad",
        /*GenCrashDiag=*/false);
  const StringRef Bytes(Idle.data(), Idle.size());
  for (unsigned I = 0; I < Count; ++I)
    emitBytes(Bytes);
}

void HaydnMCELFStreamer::emitCodeAlignment(Align Alignment,
                                           const MCSubtargetInfo *STI,
                                           unsigned MaxBytesToEmit) {
  // W70.2r product function-entry path (HasFunctionAlignment=true) and
  // hand-asm .p2align. Compiler internal MBB/ZOL alignment is GR1.9
  // padInternalMBBAlignment complete packets plus MF.ensureAlignment
  // (D1.166) so this pre-label fill is the absolute llvm.loop.align grid;
  // MBB metadata is cleared before freeze so generic emitBasicBlockStart
  // does not call this for those sites. Peer:
  // AIETargetELFStreamer::finish emitCodeAlignment(Align(16))
  // (AIETargetELFStreamer.cpp:73-81) because AIE bundles are 2^n. Format E
  // EncodedBytes is 12, so writeNopData rejects 4/8-byte fills. Advance by
  // whole idle parcels until the current text offset satisfies the requested
  // power-of-two (and therefore stays 0 mod EncodedBytes when it started
  // that way).
  MCSection *Sec = getCurrentSectionOnly();
  const unsigned Parcel = haydnProductionParcelBytes().Value;
  // Text align: only whole Format E idle parcels from a parcel-aligned
  // origin. A 4/8-byte writeNopData fill, or an idle walk that starts
  // mid-parcel, would put later B/JAL labels off the record grid.
  if (Sec && Sec->isText() && Alignment.value() > 1) {
    // Always promote sh_addralign so LLD honors aligned(N) when the
    // function sits at section offset 0 (function-sections / first
    // symbol). Returning early used to leave AddressAlignment=1 and
    // cb146_overaligned_function exited 1.
    Sec->ensureMinAlignment(Alignment);
    uint64_t Off = 0;
    for (const MCFragment &F : *Sec)
      Off += F.getSize();
    const uint64_t A = Alignment.value();
    if ((Off % A) == 0)
      return;
    // Off-grid text cannot be repaired with whole idle parcels.
    if (Parcel == 0 || (Off % Parcel) != 0) {
      // Data objects in the default text section (NatureDSP DISCARD_FUN
      // `.type @object` + `.long`, no instruction yet) are not a packet
      // stream. Pad with zeros — do not invent a 4/8-byte idle encoding.
      // Once a product instruction has been emitted, keep the pad-only
      // NEG: writeNopData refuses the short remainder.
      if (!SectionsWithInstructions.contains(Sec)) {
        MCELFStreamer::emitValueToAlignment(Alignment, 0, 1, MaxBytesToEmit);
        return;
      }
      MCELFStreamer::emitCodeAlignment(Alignment, STI, MaxBytesToEmit);
      return;
    }
    // Next address that is 0 mod Align and 0 mod EncodedBytes is 0 mod
    // lcm(Align, EncodedBytes). Worst case from a parcel-aligned offset
    // is Align/gcd parcels (offset 96 → 768 needs 56). AsmPrinter
    // defaults MaxBytesToEmit to Alignment.value() (256), which is only
    // 21 parcels and used to leave the label at 288 — off the 256 grid.
    // Always walk the lcm bound; do not consult MaxBytesToEmit.
    const unsigned Gcd = std::gcd(Parcel, static_cast<unsigned>(A));
    const unsigned MaxParcels = static_cast<unsigned>(A) / Gcd;
    unsigned Guard = 0;
    while ((Off % A) != 0 && Guard < MaxParcels) {
      emitIdleParcels(1);
      Off += Parcel;
      ++Guard;
    }
    if ((Off % A) != 0)
      report_fatal_error(
          Twine("Haydn MC: cannot reach align ") + Twine(A) +
              " from offset " + Twine(Off - Guard * Parcel) +
              " with whole Format E idle parcels",
          /*GenCrashDiag=*/false);
    requireTextParcelGrid();
    return;
  }
  MCELFStreamer::emitCodeAlignment(Alignment, STI, MaxBytesToEmit);
}

static uint64_t currentSectionSize(const MCSection *Sec) {
  if (!Sec)
    return 0;
  uint64_t Off = 0;
  for (const MCFragment &F : *Sec)
    Off += F.getSize();
  return Off;
}

void HaydnMCELFStreamer::requireTextParcelGrid() {
  MCSection *Sec = getCurrentSectionOnly();
  if (!Sec || !Sec->isText())
    return;
  const unsigned Parcel = haydnProductionParcelBytes().Value;
  const uint64_t Off = currentSectionSize(Sec);
  if (Off % Parcel == 0)
    return;
  report_fatal_error(
      Twine("Haydn MC: executable offset ") + Twine(Off) +
          " is not an exact Format E record (EncodedBytes=" +
          Twine(Parcel) + ") — refuse short pad",
      /*GenCrashDiag=*/false);
}

void HaydnMCELFStreamer::emitInstruction(const MCInst &Inst,
                                         const MCSubtargetInfo &STI) {
  // base MCStreamer::emitInstruction only visits top-level isExpr
  // operands. Without this walk, `{ jal lr, main; nop; nop }` emitted a
  // R_HAYDN_CallSImm20 against symbol index 0 (lld patched S=0 → offset -PC).
  // Hexagon: HexagonMCELFStreamer::emitInstruction → EmitSymbol per packet
  // member before MCObjectStreamer::emitInstruction.
  emitSymbolsInInst(Inst);
  MCSection *Sec = getCurrentSectionOnly();
  if (Sec && Sec->isText())
    SectionsWithInstructions.insert(Sec);
  const uint64_t Before = currentSectionSize(Sec);
  const unsigned Parcel = haydnProductionParcelBytes().Value;
  MCELFStreamer::emitInstruction(Inst, STI);
  // Product instruction size is whole EncodedBytes records. Do not require
  // the absolute text offset to be on-grid here: hand-asm reloc/pad probes
  // (.byte gaps) must still reach applyFixup / writeNopData. emitCodeAlignment
  // is the product pad owner (idle parcels only).
  if (Sec && Sec->isText()) {
    const uint64_t After = currentSectionSize(Sec);
    if (After > Before && ((After - Before) % Parcel) != 0)
      report_fatal_error(
          Twine("Haydn MC: instruction emitted ") + Twine(After - Before) +
              " bytes; product records are EncodedBytes=" + Twine(Parcel),
          /*GenCrashDiag=*/false);
  }
}

MCStreamer *llvm::createHaydnELFStreamer(const Triple & /*TT*/,
                                         MCContext &Context,
                                         std::unique_ptr<MCAsmBackend> &&MAB,
                                         std::unique_ptr<MCObjectWriter> &&OW,
                                         std::unique_ptr<MCCodeEmitter> &&CE) {
  return new HaydnMCELFStreamer(Context, std::move(MAB), std::move(OW),
                                std::move(CE));
}
