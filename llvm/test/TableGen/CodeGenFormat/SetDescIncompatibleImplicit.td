//===- SetDescIncompatibleImplicit.td ----------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Negative: MultiSlot_Pseudo materializableInto rejects a member whose
// implicit Uses/Defs set differs from the logical. MI.setDesc does not
// rebuild implicits, so a mismatch would leave hidden physreg deps wrong
// after the post-RA descriptor swap.
//
//===----------------------------------------------------------------------===//

// RUN: not llvm-tblgen --gen-instr-format -I %p -I %p/../../../include %s 2>&1 | FileCheck %s

include "Inputs/CommonFormatTesting.td"

// Compatible-looking S0 member (no implicits).
def IMP_S0 : TESTSlotInst<s0_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                          "imp", "$rd, $rs1, $rs2"> {
  let opc = 8;
}

// Same explicit operand shape, but member claims an implicit use the logical
// does not list — full setDesc contract must reject.
def IMP_S1_USE : TESTSlotInst<s1_slot, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                              "imp", "$rd, $rs1, $rs2"> {
  let opc = 8;
  let Slot = s1_slot;
  let Uses = [R0];
}

def IMP : TESTMultiSlot_Pseudo<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                               [IMP_S0, IMP_S1_USE], "imp",
                               "$rd, $rs1, $rs2">;

// CHECK: setDesc-incompatible logical{{.*}}member pair: IMP {{.*}} IMP_S1_USE
// CHECK-SAME: implicit Uses mismatch
