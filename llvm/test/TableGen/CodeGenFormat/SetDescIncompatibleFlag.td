//===- SetDescIncompatibleFlag.td --------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Negative: MultiSlot_Pseudo materializableInto rejects a member that sets a
// semantic flag (mayLoad) the logical does not cover. After MI.setDesc the
// MachineInstr keeps the logical's operand vector and inherits the member's
// descriptor flags — gaining mayLoad without an MMO would trip verifiers and
// mislead mem-deps.
//
//===----------------------------------------------------------------------===//

// RUN: not llvm-tblgen --gen-instr-format -I %p -I %p/../../../include %s 2>&1 | FileCheck %s

include "Inputs/CommonFormatTesting.td"

// Compatible-looking S0 member (mayLoad stays 0 from TESTInst defaults).
def FLG_S0 : TESTSlotInst<s0_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                          "flg", "$rd, $rs1, $rs2"> {
  let opc = 9;
}

// Same operand shape, but member mayLoad=1 while logical remains mayLoad=0.
def FLG_S1_LOAD : TESTSlotInst<s1_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                               "flg", "$rd, $rs1, $rs2"> {
  let opc = 9;
  let Slot = s1_slot;
  let mayLoad = 1;
}

// Logical inherits mayLoad=0 from TESTInst — member bit is not covered.
def FLG : TESTMultiSlot_Pseudo<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                               [FLG_S0, FLG_S1_LOAD], "flg",
                               "$rd, $rs1, $rs2">;

// CHECK: setDesc-incompatible logical{{.*}}member pair: FLG {{.*}} FLG_S1_LOAD
// CHECK-SAME: member sets mayLoad but logical does not
