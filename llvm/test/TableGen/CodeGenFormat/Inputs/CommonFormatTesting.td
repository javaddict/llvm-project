//===- CommonFormatTesting.td ------------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Minimal target boilerplate for CodeGenFormat setDesc-compat tests.
// Provides slots, a tiny register class, MultiSlot_Pseudo, and a 16-bit
// InstFormat base sufficient for gen-instr-format.
//
// Multi-row packet / alternate-slot format hierarchies live in
// CommonPacketFormatTesting.td (used by FormatTests).
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"
include "llvm/Target/CodeGenFormat.td"

def TESTInstrInfo : InstrInfo;

def TEST : Target {
  let InstructionSet = TESTInstrInfo;
}

def TESTCodeGenFormat : CodeGenFormat {
  let FormatClassEmitted = "";
}

// --- Slots ---
// FieldToFind = "Inst": single-slot test members use a flat Inst field
// (product Haydn uses s0/s1/s2_base hierarchies; tests only need setDesc shape).
let Namespace = "TEST" in {
  def default_slot : InstSlot<"default", 0, true>;
  def s0_slot : InstSlot<"s0", 16> { let FieldToFind = "Inst"; }
  def s1_slot : InstSlot<"s1", 16> { let FieldToFind = "Inst"; }
  def s2_slot : InstSlot<"s2", 16> { let FieldToFind = "Inst"; }
}

// --- Minimal regclass ---
def R0 : Register<"r0">;
def R1 : Register<"r1">;
def GPR : RegisterClass<"TEST", [i32], 32, (add R0, R1)>;

// --- Itineraries (for sched/uops coverage in setDesc tests) ---
def SLOT0 : FuncUnit;
def SLOT1 : FuncUnit;
def SLOT2 : FuncUnit;
def ItinALU : InstrItinClass;
def ItinALUSlow : InstrItinClass;
def ItinLoad : InstrItinClass;

def TESTItineraries : ProcessorItineraries<
  [SLOT0, SLOT1, SLOT2], [], [
    InstrItinData<ItinALU, [InstrStage<1, [SLOT0, SLOT1, SLOT2]>], [1]>,
    InstrItinData<ItinALUSlow, [InstrStage<1, [SLOT0]>], [3]>,
    InstrItinData<ItinLoad, [InstrStage<1, [SLOT0]>], [2]>
  ]>;

def TESTModel : SchedMachineModel {
  let IssueWidth = 3;
  let LoadLatency = 2;
  let CompleteModel = 0;
  let Itineraries = TESTItineraries;
}

def TESTProc : Processor<"test", TESTItineraries, []>;

// --- Instruction bases ---
// Inst must live on a superclass so CodeGenFormat hierarchy walk finds it
// (peer of AIE CommonFormatTesting TEST_instr* classes).
class TESTInst<dag outs, dag ins, string opcodestr, string argstr>
    : Instruction, InstFormat {
  let Namespace = "TEST";
  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = opcodestr # "\t" # argstr;
  let Pattern = [];
  let Slot = default_slot;
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;
  field bits<16> SoftFail = 0;
  field bits<16> Inst;
  let Size = 2;
}

// Single-slot format member (16-bit Inst).
class TESTSlotInst<InstSlot slot, dag outs, dag ins, string opcodestr,
                   string argstr>
    : TESTInst<outs, ins, opcodestr, argstr> {
  let isComposite = false;
  let Slot = slot;
  bits<4> opc = 0;
  bits<4> rd = 0;
  bits<4> rs1 = 0;
  bits<4> rs2 = 0;
  let Inst = {opc, rd, rs1, rs2};
  let Itinerary = ItinALU;
}

// Multi-slot logical (AIE MultiSlot_Pseudo / Haydn peer).
class TESTMultiSlot_Pseudo<dag outs, dag ins,
                           list<Instruction> insts = [],
                           string opcodestr = "", string argstr = "">
    : TESTInst<outs, ins, opcodestr, argstr> {
  let isPseudo = 1;
  bits<1> isMultiSlotPseudo = 1;
  list<Instruction> materializableInto = insts;
  let isCodeGenOnly = 1;
  let isComposite = 0;
  let Itinerary = ItinALU;
}
