//===- HaydnMachineAlignment.cpp --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License, v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// GR1.9 / GR1.4: materialize internal MBB/ZOL alignment as complete
// generated packets. The MachineFunctionPass class is deleted (GR1.7 /
// GR2.10). Stamped closer waves call padInternalMBBAlignment with
// ClearMetadata=false so alignment stays first in the exact-layout loop.
//
// Peer: AIEMachineAlignment.cpp:370-424 walks layout order, pads the
// previous region so the next MBB/ZOL body starts aligned, then
// verifyAlignment / verifyDistanceToLoopEnd (:226-358, :370-423).
// AIEBaseInstrInfo.cpp:507-514 isZOLBody (last non-debug is loop end);
// :1779 getAlignmentBoundaries.
//
// Haydn overlay:
//   * AIE elongates variable-width bundles (AIEMachineAlignment.cpp:53-208).
//     EncodedBytes is a fixed parcel, so padding is one idle row per parcel
//     (TII.insertNoop + finalizeExactLateSingleton — same construction as
//     LatencyStalls stall parcels / HexagonInstrInfo.cpp:1667-1671).
//   * Function-entry alignment is pre-label MC fill (W70.2r). Do not insert
//     post-label idle parcels to move the function symbol. Sites with A > 1
//     call MF.ensureAlignment(A) so that fill + section sh_addralign honor
//     llvm.loop.align as an absolute address (D1.166). Min/Pref stay Align(4).
//   * Jump-target / ZOL hardware alignment is the EncodedBytes grid plus
//     golden 2-byte bundle min / Off <<2, not AIE's 16-byte MBB pad. A
//     committed packet stream is already 0 mod 12. Non-grid offsets fail
//     closed rather than inventing a 4/8-byte pad.
//   * Offset walk charges committed packet bytes (BUNDLE row EncodedBytes,
//     or getInstSizeInBytes for still-bare MIR). Named late-layout growth
//     on SET_HWLOOP is not yet a packet and must not shift the pad grid.
//   * Closure consumes MBB.getAlignment() (MBP llvm.loop.align / -align-loops)
//     by inserting parcels in the layout predecessor before its terminator
//     tail, then (when ClearMetadata) setAlignment(Align(1)) so
//     AsmPrinter::emitBasicBlockStart cannot emit residual emitCodeAlignment
//     after freeze.
//
// Byte law: committed EncodedBytes on BUNDLE roots (children/meta 0).
// Inserted parcels are therefore charged by every later EncodedBytes walk.
//
//===----------------------------------------------------------------------===//

#include "HaydnMachineAlignment.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <iterator>
#include <numeric>

using namespace llvm;

#define DEBUG_TYPE "haydn-internal-alignment"

STATISTIC(NumInternalSitesPadded,
          "Internal MBB sites padded with idle parcels");
STATISTIC(NumIdleParcelsInserted, "Idle parcels inserted for internal alignment");
STATISTIC(NumMBBAlignmentsCleared,
          "MBB alignment metadata records cleared before freeze");
STATISTIC(NumFunctionAlignmentsRaised,
          "Function alignments raised for internal MBB / llvm.loop.align");

namespace {

/// AIE isZOLBody peer (AIEBaseInstrInfo.cpp:507-514): last non-debug
/// instruction is the hardware-loop end meta.
static bool isZOLLatch(const MachineBasicBlock &MBB) {
  auto Last = MBB.getLastNonDebugInstr();
  if (Last == MBB.end())
    return false;
  return Last->getOpcode() == Haydn::PseudoLoopEnd;
}

/// Committed executable bytes in \p MBB. BUNDLE roots use stamped EncodedBytes
/// (not getInstSizeInBytes, which adds named late-layout growth on SET_HWLOOP
/// that is not a packet yet). Bare MIR (-run-pass) still uses getInstSizeInBytes.
static uint64_t mbbEncodedBytes(const MachineBasicBlock &MBB,
                                const HaydnInstrInfo &TII) {
  uint64_t Bytes = 0;
  for (const MachineInstr &MI : MBB) {
    if (MI.isBundle())
      Bytes += haydn::bundle::committedEncodedBytes(MI).Value;
    else
      Bytes += TII.getInstSizeInBytes(MI);
  }
  return Bytes;
}

/// Insert before the ordered terminator tail so the predecessor still ends
/// on its control transfer (D1.50). Fallthrough blocks append at end.
/// getFirstTerminator walks bundle roots, so the idle parcel is never
/// spliced into a committed packet.
static MachineBasicBlock::iterator
alignmentPadInsertPt(MachineBasicBlock &MBB) {
  return MBB.getFirstTerminator();
}

/// One committed idle parcel (full-slot architectural NOP row) before
/// \p InsertPt. Mirrors LatencyStalls insertStalls wrap: insertNoop, bake,
/// exact-late singleton. Not a stall-regeneration NoMerge pin.
static unsigned insertIdleParcel(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator InsertPt,
                                 const HaydnInstrInfo &TII) {
  TII.insertNoop(MBB, InsertPt);
  MachineInstr &Nop = *std::prev(InsertPt);
  haydn::bundle::applyFinalDirectCompatibleSingleton(Nop, haydnDefaultMCFormats(),
                                                     TII);
  haydn::bundle::finalizeExactLateSingleton(Nop);
  MachineInstr &Root = *getBundleStart(Nop.getIterator());
  if (!Root.isBundle())
    report_fatal_error(
        "HaydnMachineAlignment: idle parcel must be a BUNDLE root",
        /*GenCrashDiag=*/false);
  const unsigned Bytes = haydn::bundle::committedEncodedBytes(Root).Value;
  const unsigned Parcel = haydn::bundle::productParcelBytes().Value;
  if (Bytes != Parcel)
    report_fatal_error(
        Twine("HaydnMachineAlignment: idle parcel charged ") + Twine(Bytes) +
            " bytes; product EncodedBytes=" + Twine(Parcel),
        /*GenCrashDiag=*/false);
  ++NumIdleParcelsInserted;
  return Bytes;
}

} // namespace

bool haydn::padInternalMBBAlignment(MachineFunction &MF,
                                    const HaydnInstrInfo &TII,
                                    bool ClearMetadata) {
  const uint64_t Parcel = haydn::bundle::productParcelBytes().Value;
  if (Parcel == 0)
    report_fatal_error("HaydnMachineAlignment: product EncodedBytes is 0",
                       /*GenCrashDiag=*/false);

  bool Changed = false;
  uint64_t Offset = 0;
  MachineBasicBlock *Prev = nullptr;

  for (MachineBasicBlock &MBB : MF) {
    if ((Offset % Parcel) != 0)
      report_fatal_error(
          Twine("HaydnMachineAlignment: executable offset ") + Twine(Offset) +
              " in " + MF.getName() + " is not an EncodedBytes grid point",
          /*GenCrashDiag=*/false);

    const Align A = MBB.getAlignment();
    if (A > Align(1)) {
      // Absolute-address grid: relative pads only work if the function
      // itself is A-aligned. W70.2r pre-label idle parcels + ensureMinAlignment
      // then land the symbol. Do not raise Min/Pref Align(4).
      if (MF.getAlignment() < A) {
        MF.ensureAlignment(A);
        Changed = true;
        ++NumFunctionAlignmentsRaised;
      }
    }

    const bool NeedsInternalPad =
        Prev && A > Align(1) && (Offset % A.value()) != 0;

    if (NeedsInternalPad) {
      const uint64_t Grid = std::lcm(A.value(), Parcel);
      const uint64_t Target = alignTo(Offset, Grid);
      if (Target < Offset)
        report_fatal_error(
            "HaydnMachineAlignment: alignment grid underflow",
            /*GenCrashDiag=*/false);
      const uint64_t PadBytes = Target - Offset;
      const uint64_t MaxParcels = A.value() / std::gcd(Parcel, A.value());
      const uint64_t Parcels = PadBytes / Parcel;
      if ((PadBytes % Parcel) != 0 || Parcels == 0 || Parcels > MaxParcels)
        report_fatal_error(
            Twine("HaydnMachineAlignment: cannot align bb.") +
                Twine(MBB.getNumber()) + " in " + MF.getName() + " (offset " +
                Twine(Offset) + ", align " + Twine(A.value()) +
                ") with legal idle parcels",
            /*GenCrashDiag=*/false);
      const unsigned MaxBytes = MBB.getMaxBytesForAlignment();
      if (MaxBytes != 0 && PadBytes > MaxBytes)
        report_fatal_error(
            Twine("HaydnMachineAlignment: bb.") + Twine(MBB.getNumber()) +
                " in " + MF.getName() + " needs " + Twine(PadBytes) +
                " pad bytes but MaxBytesForAlignment=" + Twine(MaxBytes),
            /*GenCrashDiag=*/false);

      LLVM_DEBUG(dbgs() << "HaydnMachineAlignment: " << MF.getName()
                        << " bb." << MBB.getNumber()
                        << (isZOLLatch(MBB) ? " (ZOL latch)" : "")
                        << " offset " << Offset << " align " << A.value()
                        << " -> " << Parcels << " idle parcel(s) in bb."
                        << Prev->getNumber() << "\n");

      MachineBasicBlock::iterator InsertPt = alignmentPadInsertPt(*Prev);
      for (uint64_t I = 0; I < Parcels; ++I)
        insertIdleParcel(*Prev, InsertPt, TII);
      Offset += PadBytes;
      Changed = true;
      ++NumInternalSitesPadded;
    } else if (isZOLLatch(MBB)) {
      LLVM_DEBUG(dbgs() << "HaydnMachineAlignment: " << MF.getName()
                        << " ZOL latch bb." << MBB.getNumber() << " at offset "
                        << Offset << " already on EncodedBytes grid\n");
    }

    Offset += mbbEncodedBytes(MBB, TII);
    Prev = &MBB;
  }

  if ((Offset % Parcel) != 0)
    report_fatal_error(
        Twine("HaydnMachineAlignment: function ") + MF.getName() +
            " extent " + Twine(Offset) + " is not an EncodedBytes grid point",
        /*GenCrashDiag=*/false);

  if (!ClearMetadata)
    return Changed;

  // Consume alignment metadata so freeze/AsmPrinter cannot grow the stream.
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.getAlignment() == Align(1) && MBB.getMaxBytesForAlignment() == 0)
      continue;
    MBB.setAlignment(Align(1));
    MBB.setMaxBytesForAlignment(0);
    ++NumMBBAlignmentsCleared;
    Changed = true;
  }

  return Changed;
}

bool llvm::padHaydnInternalAlignment(MachineFunction &MF, bool ClearMetadata) {
  const HaydnInstrInfo &TII =
      *MF.getSubtarget<HaydnSubtarget>().getInstrInfo();
  return haydn::padInternalMBBAlignment(MF, TII, ClearMetadata);
}
