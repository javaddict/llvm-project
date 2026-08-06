//===- SetDescIncompatible.td ------------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Negative: a deliberately setDesc-unsafe MultiSlot_Pseudo member
// (operand-count mismatch) must be rejected by CodeGenFormat with a clear
// diagnostic. Full contract (shape/flag/sched) is hard-fail for explicit
// materializableInto lists.
//
//===----------------------------------------------------------------------===//

// RUN: not llvm-tblgen --gen-instr-format -I %p -I %p/../../../include %s 2>&1 | FileCheck %s

include "Inputs/CommonFormatTesting.td"

// Compatible-looking S0 member.
def BAD_S0 : TESTSlotInst<s0_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                          "bad", "$rd, $rs1, $rs2"> {
  let opc = 7;
}

// Deliberately fewer operands than the logical (2-op vs 3-op) — setDesc would
// leave a dangling MO and trip -verify-machineinstrs (L242/L254).
def BAD_S1_SHORT : TESTSlotInst<s1_slot, (outs GPR:$rd), (ins GPR:$rs1),
                                "bad", "$rd, $rs1"> {
  let opc = 7;
  let Slot = s1_slot;
}

// Logical declares both members via materializableInto → full setDesc gate.
def BAD : TESTMultiSlot_Pseudo<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                               [BAD_S0, BAD_S1_SHORT], "bad",
                               "$rd, $rs1, $rs2">;

// CHECK: setDesc-incompatible logical{{.*}}member pair: BAD {{.*}} BAD_S1_SHORT
// CHECK-SAME: operand count mismatch
