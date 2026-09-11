//===- HaydnMachineAlignment.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Internal MBB/ZOL alignment as complete generated packets (GR1.9 / GR1.4).
// Library, not a MachineFunctionPass. Stamped LBN calls padInternalMBBAlignment
// with ClearMetadata=false at each closer wave and ClearMetadata=true after
// the loop so EncodedBytes pads and MBB metadata clear finish in
// addPreEmitPass.
//
// Function-entry alignment is pre-label MC fill (W70.2r):
// HasFunctionAlignment=true → AsmPrinter::emitAlignment →
// HaydnMCELFStreamer::emitCodeAlignment. That fill sits outside every
// function's committed stream. Do not reintroduce a post-label function
// extent pad here. llvm.loop.align / internal MBB alignment raise
// MF.ensureAlignment so that pre-label fill is an absolute-address grid.
//
// Peer: AIEMachineAlignment.cpp:370-424 (padRegions after Finalize).
// Overlay vs AIE elongation (AIEMachineAlignment.cpp:53-208): EncodedBytes
// is a fixed 12-byte parcel, so padding is insertNoop + exact-late singleton
// wrap (HexagonInstrInfo.cpp:1667-1671 / HaydnLatencyStalls stall parcels),
// never format growth. Consumed MBB alignment metadata is cleared so
// generic AsmPrinter::emitBasicBlockStart cannot add bytes after freeze.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNMACHINEALIGNMENT_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNMACHINEALIGNMENT_H

namespace llvm {

class HaydnInstrInfo;
class MachineFunction;

namespace haydn {

/// Insert complete EncodedBytes idle packets so each MBB with alignment A
/// starts at 0 mod lcm(A, EncodedBytes) from function start. Raises
/// MF.ensureAlignment(A) per site with A > 1 so W70.2r pre-label fill +
/// HaydnAsmPrinter::ensureMinAlignment honor llvm.loop.align as an absolute
/// address. Does not change Min/Pref Align(4).
///
/// \p ClearMetadata: stamped closer waves pass false at the start of each
/// wave (alignment, then inner-first HWLoop, then branches) so later range
/// growth can re-pad. Stamped LBN passes true after the closer loop so
/// freeze cannot emit residual emitCodeAlignment.
bool padInternalMBBAlignment(MachineFunction &MF, const HaydnInstrInfo &TII,
                             bool ClearMetadata);

} // namespace haydn

/// Convenience wrapper that looks up TII. Stamped LBN calls this with
/// ClearMetadata=false at the start of each closer wave and true after.
bool padHaydnInternalAlignment(MachineFunction &MF, bool ClearMetadata);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNMACHINEALIGNMENT_H
