//===- HaydnMCELFStreamer.h - Haydn subclass of MCELFStreamer ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn VLIW bundles (Haydn::BUNDLE) carry slot children as MCOperand::isInst
// sub-instructions. The base MCStreamer::emitInstruction only walks top-level
// isExpr operands, so symbols referenced *inside* a bundle child (e.g.
// `{ jal lr, main; nop; nop }`) are never registered with the assembler.
// Unregistered symbols are omitted from.symtab; the ELF reloc then points at
// symbol index 0 (ABS), and lld patches S=0 → a garbage PC-relative offset
// (: crt0 `jal lr, main` → `jal r0, 65535`).
//
// Mirror HexagonMCELFStreamer: before the base emit path, recursively visit
// used expressions in every bundle child so registerSymbol/setUsedInReloc run.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCELFSTREAMER_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCELFSTREAMER_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/MC/MCELFStreamer.h"
#include <memory>

namespace llvm {

class HaydnMCELFStreamer : public MCELFStreamer {
public:
  HaydnMCELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                     std::unique_ptr<MCObjectWriter> OW,
                     std::unique_ptr<MCCodeEmitter> Emitter);

  void emitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo &STI) override;

  // Product pack: executable padding is whole Format E parcels only.
  // W70.2r function-entry path: HasFunctionAlignment=true makes AsmPrinter
  // call this before the entry label (shared .text and optional function
  // sections). Peer: AIETargetELFStreamer::finish emitCodeAlignment(Align(16)).
  // EncodedBytes is not a power of two, so a 4/8-byte align fragment is
  // rounded up to idle parcels instead of writeNopData failing or inventing
  // a short pad. Walk Align/gcd parcels (lcm bound); ignore MaxBytesToEmit.
  // Data-only text (no instruction yet) zero-fills an off-grid remainder;
  // instruction text stays fail-closed.
  //
  // GR1.9 / D1.166: compiler internal MBB/ZOL alignment is committed idle
  // packets from padInternalMBBAlignment, which also MF.ensureAlignment
  // so this pre-label fill honors llvm.loop.align as an absolute address.
  // MBB metadata is cleared before freeze. This override remains the
  // function-entry pre-label owner and the hand-asm .p2align path. It must
  // not become a second compiler layout owner for unresolved MBB alignment
  // metadata.
  void emitCodeAlignment(Align Alignment, const MCSubtargetInfo *STI,
                         unsigned MaxBytesToEmit = 0) override;

  // Compiler function labels must sit on an exact Format E record. Align(4)
  // cannot restore a broken 12-byte grid; refuse rather than invent a short
  // pad (idle parcels only change the offset by EncodedBytes).
  void requireTextParcelGrid();

private:
  // Recursively register symbols referenced by Inst (and any isInst children).
  void emitSymbolsInInst(const MCInst &Inst);
  void emitIdleParcels(unsigned Count);

  // Text sections that have emitted a product instruction. Data-only text
  // (Xtensa DISCARD_FUN `.long` stubs) may zero-pad to `.align`; instruction
  // streams stay fail-closed on a short writeNopData remainder.
  SmallPtrSet<const MCSection *, 4> SectionsWithInstructions;
};

MCStreamer *createHaydnELFStreamer(const Triple &TT, MCContext &Context,
                                   std::unique_ptr<MCAsmBackend> &&MAB,
                                   std::unique_ptr<MCObjectWriter> &&OW,
                                   std::unique_ptr<MCCodeEmitter> &&CE);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCELFSTREAMER_H
