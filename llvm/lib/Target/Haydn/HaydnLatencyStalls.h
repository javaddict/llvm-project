//===- HaydnLatencyStalls.h - Exposed-pipeline RAW stall insertion -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn has an EXPOSED pipeline: there is no interlock. The ISA rule is
//
//   "If instruction A at bundle t has Data_Latency = N, no instruction in
//    bundles t+1 through t+N-1 may read A's destination register."
//
// Loads (and CSRR, and the MAC family) carry Data_Latency = 2, so a load's
// destination must not be read in the very next bundle. Schedulers already
// see architectural load→use latency (no soften); this library remains the
// dest-window correctness net:
//
//   * Never calls skipFunction: latency is correctness, not quality.
//   * Product S1 folds this net into HaydnPostRASchedStrategy so stall
//     NOPs and the CFG/inventory stamp arise with the first complete
//     packets. Unstamped Finalize calls the same function. There is no
//     MachineFunctionPass seat.
//
// BundleSim cannot catch this either — it is a purely functional bundle
// simulator with no timing model, so a violating program still produces the
// right answer in simulation and the wrong answer on hardware.
//
// Walk each block in bundle order and insert NOP stall bundles wherever a
// read would land inside a producer's latency window. Product S1 runs this
// once at PostMachineScheduler exit before the CFG stamp. EncodedBytes cap
// is fail-closed: a wrap/exit pad that would walk a still-short site past
// simm12/simm20 or a formation-kept ZOL past Off1/Off2 is a named fatal
// (AIE/Hexagon have no EncodedBytes stall-cap peer; size-neutral same-row
// NOP wrap cannot add a cycle).
//
// D1.16 loop back-edge wrap law (this is the always-on emission net):
// a latch MBB (self-successor) re-executes its own cycle 0 immediately
// after its last body cycle — across the HWLR_END -> HWLR_BEGIN wrap for
// ZOL loops, across the backedge branch for soft loops. The required pad
// is the same max-remaining value the exit seam computes
// (HaydnHazardRecognizer::destWindowWrapPadNeed == destWindowExitLeak);
// only the INSERTION POINT differs. ZOL pads go before the END-anchored
// parcel so they execute INSIDE [BEGIN,END] (parcels after END execute
// only on loop exit); soft pads go before the backedge branch, which also
// covers the exit path — a soft latch inserts once, never twice.
//
// Inserted parcels are charged with TargetInstrInfo::getInstSizeInBytes —
// the same size interface branch / HWLoop range checks consume — plus the
// generated minimum bundle-address alignment remainder. They carry empty
// MMOs (NOP is not a memory op) and no extra kill/dead / implicit liveness.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNLATENCYSTALLS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNLATENCYSTALLS_H

namespace llvm {

class MachineFunction;

/// One dest-window stall net plus leftover-logical identity bake of
/// remaining *bare* FieldSlot MIs. HaydnPostRASchedStrategy runs leftover
/// RET expand, then this net, then bundled leftover-logical inverse bake
/// + stamp. Unstamped Finalize calls the same function.
bool haydnInsertExposedPipelineStalls(MachineFunction &MF);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNLATENCYSTALLS_H
