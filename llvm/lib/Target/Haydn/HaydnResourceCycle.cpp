//===- HaydnResourceCycle.cpp - SMS resource-cycle port demand ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Translation unit for HaydnResourceCycle. Class methods that are
// templates or SMS probes stay header-inline; this TU owns the pair with
// HaydnResourceCycle.h (AIE AIEHazardRecognizer.cpp:181-222 AIEResourceCycle
// peer). SIN_COS occupancy is booked on reserveResources(MI) via
// haydnSinCosWindowOccupancy (HR scoreboard peer).
//
//===----------------------------------------------------------------------===//

#include "HaydnResourceCycle.h"

// Non-template pair for HaydnResourceCycle.h. Template / SMS probe methods
// stay header-inline. Port-demand helpers remain in the header so unit
// pins can see the per-field MOVE32 constants at compile time.
//
// Compile-time pins: SFR 2R/1W lives on HaydnCyclePortDemand; CompleteModel
// and memory-object wait stay fail-closed. AIE AIEResourceCycle
// (AIEHazardRecognizer.cpp:181-222) is the class-shape peer.
namespace {
using llvm::HAYDN_SFR_READ_PORTS;
using llvm::HAYDN_SFR_WRITE_PORTS;
using llvm::haydnMemoryObjectWaitCyclesAdmitted;
using llvm::haydnSchedCompleteModelPin;
static_assert(HAYDN_SFR_READ_PORTS == 2 && HAYDN_SFR_WRITE_PORTS == 1,
              "SFR port law is 2R/1W");
static_assert(llvm::HaydnMove32ClassMiRepeatedSrcGprReads == 1 &&
                  llvm::HaydnMove32ClassDescShapeGprReads == 1,
              "MOVE32 rd,rs is 1R on MI and descriptor paths");
static_assert(!llvm::haydnMove32ClassDescOvercountsMiPorts(),
              "per-field MOVE32 has no MI-versus-descriptor read gap");
static_assert(!haydnMemoryObjectWaitCyclesAdmitted(),
              "memory-object wait-cycle reject stays unadmitted");
static_assert(haydnSchedCompleteModelPin() == 0,
              "CompleteModel stays 0 until admitted per-op records");
} // namespace
