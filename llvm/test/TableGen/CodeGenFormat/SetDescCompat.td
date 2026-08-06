//===- SetDescCompat.td ------------------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Positive: logical→member pairs that are setDesc-safe (matching operand
// shape/ties, implicits, flags, and covering itinerary latency/uops) are
// accepted and emitted into GET_ALTERNATE_INST_OPCODE_FUNC.
//
//===----------------------------------------------------------------------===//

// RUN: llvm-tblgen --gen-instr-format -I %p -I %p/../../../include %s 2>&1 | FileCheck %s

include "Inputs/CommonFormatTesting.td"

// Compatible RR members across S0/S1/S2 (same outs/ins, flags, ItinALU).
def ADD_S0 : TESTSlotInst<s0_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                          "add", "$rd, $rs1, $rs2"> {
  let opc = 1;
  let Slot = s0_slot;
}
def ADD_S1 : TESTSlotInst<s1_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                          "add", "$rd, $rs1, $rs2"> {
  let opc = 1;
  let Slot = s1_slot;
}
def ADD_S2 : TESTSlotInst<s2_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                          "add", "$rd, $rs1, $rs2"> {
  let opc = 1;
  let Slot = s2_slot;
}

// Explicit MultiSlot_Pseudo — full setDesc contract (shape/flag/sched).
def ADD : TESTMultiSlot_Pseudo<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                               [ADD_S0, ADD_S1, ADD_S2], "add",
                               "$rd, $rs1, $rs2">;

// Sparse name-discovered logical + members (also setDesc-structurally safe).
def XOR : TESTInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "xor",
                   "$rd, $rs1, $rs2"> {
  let isComposite = false;
  let Inst = 0;
  let Slot = default_slot;
  let Itinerary = ItinALU;
}
def XOR_S0 : TESTSlotInst<s0_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                          "xor", "$rd, $rs1, $rs2"> { let opc = 2; }
def XOR_S1 : TESTSlotInst<s1_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                          "xor", "$rd, $rs1, $rs2"> { let opc = 2; }
def XOR_S2 : TESTSlotInst<s2_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                          "xor", "$rd, $rs1, $rs2"> { let opc = 2; }

// Load: mayLoad covered on logical and members; latency ItinLoad [2].
def LD : TESTInst<(outs GPR:$rd), (ins GPR:$rs1), "ld", "$rd, $rs1"> {
  let isComposite = false;
  let Inst = 0;
  let Slot = default_slot;
  let mayLoad = 1;
  let Itinerary = ItinLoad;
}
def LD_S0 : TESTSlotInst<s0_slot, (outs GPR:$rd), (ins GPR:$rs1),
                         "ld", "$rd, $rs1"> {
  let opc = 3;
  let mayLoad = 1;
  let Itinerary = ItinLoad;
}

// CHECK: GET_ALTERNATE_INST_OPCODE_FUNC
// CHECK: setDesc-compatible
// MultiSlot dense path for ADD + sparse XOR/LD.
// CHECK: case TEST::ADD:
// CHECK: case TEST::LD:
// CHECK: case TEST::XOR:
// Members present (non-zero).
// CHECK-DAG: TEST::ADD_S0
// CHECK-DAG: TEST::ADD_S1
// CHECK-DAG: TEST::ADD_S2
// CHECK-DAG: TEST::XOR_S0
// CHECK-DAG: TEST::LD_S0
