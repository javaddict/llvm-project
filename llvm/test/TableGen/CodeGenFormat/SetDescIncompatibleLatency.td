//===- SetDescIncompatibleLatency.td -----------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Negative: MultiSlot_Pseudo materializableInto rejects a member whose
// itinerary OperandCycles are not covered by the logical. Post-RA setDesc
// keeps the original MI schedule assumptions; a slower member would under-
// estimate dependency latency / uop cost after the descriptor swap.
//
// CommonFormatTesting provides ItinALU (OperandCycles [1]) and ItinALUSlow
// (OperandCycles [3]) for this coverage check.
//
//===----------------------------------------------------------------------===//

// RUN: not llvm-tblgen --gen-instr-format -I %p -I %p/../../../include %s 2>&1 | FileCheck %s

include "Inputs/CommonFormatTesting.td"

// Compatible S0 member on the fast ALU itinerary.
def LAT_S0 : TESTSlotInst<s0_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                          "lat", "$rd, $rs1, $rs2"> {
  let opc = 10;
  let Itinerary = ItinALU;
}

// Same shape, but member is slower (OperandCycles[0] = 3) than logical (= 1).
def LAT_S1_SLOW : TESTSlotInst<s1_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                               "lat", "$rd, $rs1, $rs2"> {
  let opc = 10;
  let Slot = s1_slot;
  let Itinerary = ItinALUSlow;
}

// Logical defaults to ItinALU via TESTMultiSlot_Pseudo — does not cover SLOW.
def LAT : TESTMultiSlot_Pseudo<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                               [LAT_S0, LAT_S1_SLOW], "lat",
                               "$rd, $rs1, $rs2"> {
  let Itinerary = ItinALU;
}

// CHECK: setDesc-incompatible logical{{.*}}member pair: LAT {{.*}} LAT_S1_SLOW
// CHECK-SAME: OperandCycles[0] not covered
