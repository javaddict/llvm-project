//===-- HaydnExpandPostIncEarly.cpp - Early post-inc expansion ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Product post-inc expansion home (R2; default ON). Expands the four Haydn
// post-increment pseudo instructions into real load/store + ADDI32 sequences
// BEFORE the VLIW packetizer runs. See HaydnExpandPostIncEarly.h and.
//
//===----------------------------------------------------------------------===//

#include "Haydn.h"
#include "HaydnExpandPostIncEarly.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

#define DEBUG_TYPE "haydn-expand-post-inc-early"

using namespace llvm;

char HaydnExpandPostIncEarly::ID = 0;

INITIALIZE_PASS_BEGIN(HaydnExpandPostIncEarly, "haydn-expand-post-inc-early",
                      "Haydn early post-increment pseudo expansion", false,
                      false)
INITIALIZE_PASS_END(HaydnExpandPostIncEarly, "haydn-expand-post-inc-early",
                    "Haydn early post-increment pseudo expansion", false, false)

HaydnExpandPostIncEarly::HaydnExpandPostIncEarly() : MachineFunctionPass(ID) {
  initializeHaydnExpandPostIncEarlyPass(*PassRegistry::getPassRegistry());
}

FunctionPass *llvm::createHaydnExpandPostIncEarlyPass() {
  return new HaydnExpandPostIncEarly();
}

bool HaydnExpandPostIncEarly::runOnMachineFunction(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<HaydnSubtarget>();
  TII = ST.getInstrInfo();

  bool Modified = false;

  // Expand every standalone post-inc pseudo in the function. Product home
 // runs in addPreSched2 after optional Experimental LoadStoreOpt
  // (form, default OFF) and BEFORE the VLIW packetizer, so no bundles exist
  // yet — every post-inc pseudo is a plain MachineInstr in its parent MBB
  // and can be expanded with the standard insert-before + erase pattern.
  //
  // Pseudos that sit inside a BUNDLE are NOT touched here: there are no
  // bundles yet. If a future pass reordering places a post-inc pseudo into a
  // bundle before this point, the late HaydnExpandPseudos in-bundle safety
  // net (expandPseudosInBundles) handles it. We do not handle bundles here to
  // keep the early/late responsibilities cleanly separated.
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
    while (MBBI != E) {
      MachineBasicBlock::iterator NextMBBI = std::next(MBBI);
      Modified |= expandMI(MBB, *MBBI);
      MBBI = NextMBBI;
    }
  }

  return Modified;
}

bool HaydnExpandPostIncEarly::expandMI(MachineBasicBlock &MBB,
                                       MachineInstr &MI) {
  // Only the four post-inc pseudos. Every other pseudo (LOAD_ADDR
  // ADJCALLSTACK*, PseudoCALL, LIBCALL_*, SET_HWLOOP, COPY, etc.) is left
  // untouched — the late HaydnExpandPseudos owns those. Keeping this pass
  // narrowly scoped avoids any interaction with the / wrong-code
  // history: no COPY ever moves through this code path.
  unsigned Opcode = MI.getOpcode();
  switch (Opcode) {
  default:
    return false;
  case Haydn::LD32_POST_INC:
  case Haydn::ST32_POST_INC:
  case Haydn::LD64_POST_INC:
  case Haydn::ST64_POST_INC:
    break;
  }

  DebugLoc DL = MI.getDebugLoc();

  // All four post-inc pseudos share the same operand layout (HaydnInstrInfo.td
  // lines 141-172). Loads: (outs DataReg), (ins BaseReg, Stride, Offset).
  // Stores: (outs), (ins DataReg, BaseReg, Stride, Offset). Index
  // carefully per shape — DO NOT touch COPY handling.
  //
  // The expansion is byte-identical to the late HaydnExpandPseudos helpers
  // (expandLD32PostInc / expandST32PostInc / expandLD64PostInc
  // expandST64PostInc). Producing the SAME instructions here means the only
  // observable difference is WHERE in the pipeline the expansion happens
  // (pre-packetizer vs post-packetizer): semantics are unchanged, and the
  // packetizer now sees real LD/LD_S1 + ADDI with real itineraries.
  switch (Opcode) {
  case Haydn::LD32_POST_INC: {
    Register DstReg = MI.getOperand(0).getReg();
    Register BaseReg = MI.getOperand(1).getReg();
    int64_t Stride = MI.getOperand(2).getImm();
    int64_t Offset = MI.getOperand(3).getImm();
    // emit the canonical DB-named S_LW_POST_IMM (s_lw_post_imm) when the
    // displacement is zero, the stride is a multiple of the 4-byte access
    // width, and the scaled stride fits in signed imm6 (-32..+31 elements).
    // The base writeback rides the AGU port (encoding_manual.md:217)
    // consumes one LS slot, no ALU/ADDI cost. Fall back to the LD32 + ADDI32
    // split for non-zero displacement, non-multiple stride, or
    // out-of-range scaled index. NOTE: the DB D_LW_POST_IMM is a DR64
    // (paired-i32) load; the scalar i32 post-inc load is S_LW_POST_IMM
    // (GPR32 dest), which is what LD32_POST_INC models. (emitted the
    // parallel-namespace LD32_POST; retires that to the DB name.)
    if (Offset == 0 && (Stride % 4) == 0 && isInt<6>(Stride >> 2)) {
      // td form: (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm)
      // MCInst operand order: [rt, rs_wb, rs, scaled_imm]. The tied
      // constraint ($rs = $rs_wb) forces regalloc to coalesce them so the
      // encoded rs field carries both at silicon time. Build both defs
      // explicitly with RegState::Define (no positional result reg — 2-def).
      BuildMI(MBB, MI, DL, TII->get(Haydn::S_LW_POST_IMM))
          .addReg(DstReg, RegState::Define)        // $rt (loaded data, GPR32)
          .addReg(BaseReg, RegState::Define)       // $rs_wb (AGU writeback)
          .addReg(BaseReg)                          // $rs (input base, tied)
          .addImm(Stride >> 2);                     // $scaled_imm (imm6 index)
      break;
    }
    // LD32 takes slot 0 (Slot0_LS); promoteLoadsToSlot1 in the packetizer will
    // still upgrade it to LD32_S1 if a preceding slot-0-LS op demands it.
    BuildMI(MBB, MI, DL, TII->get(Haydn::LD32), DstReg)
        .addReg(BaseReg)
        .addImm(Offset);
    BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), BaseReg)
        .addReg(BaseReg)
        .addImm(Stride);
    break;
  }
  case Haydn::ST32_POST_INC: {
    Register DataReg = MI.getOperand(0).getReg();
    Register BaseReg = MI.getOperand(1).getReg();
    int64_t Stride = MI.getOperand(2).getImm();
    int64_t Offset = MI.getOperand(3).getImm();
    // emit the fused ST32_POST (S_SW_POST_IMM semantics — store 32b rt
    // to [rs], then rs += imm6<<2) when the displacement is zero, the stride
    // is a multiple of the 4-byte access width, and the scaled stride fits in
    // signed imm6 (-32..+31 elements). The base writeback rides the AGU port
    // exactly as the load form (S_LW_POST_IMM) — consumes one LS slot, no
    // ALU/ADDI cost. This eliminates the ST32 + ADDI32 split (2 instrs in 2
    // slots) into a single instruction, saving one bundle slot per streaming
    // store. Fall back to the split for non-zero displacement, non-multiple
    // stride, or out-of-range scaled index.
    // td form: (outs GPR32:$rs_wb), (ins GPR32:$rt, GPR32:$rs, simm6:$imm)
    // MCInst operand order: [rt, rs_wb, rs, scaled_imm]
    if (Offset == 0 && (Stride % 4) == 0 && isInt<6>(Stride >> 2)) {
      BuildMI(MBB, MI, DL, TII->get(Haydn::ST32_POST))
          .addReg(BaseReg, RegState::Define)       // $rs_wb (AGU writeback)
          .addReg(DataReg)                          // $rt (stored data, GPR32)
          .addReg(BaseReg)                          // $rs (input base, tied)
          .addImm(Stride >> 2);                     // $scaled_imm (imm6 index)
      break;
    }
    BuildMI(MBB, MI, DL, TII->get(Haydn::ST32))
        .addReg(DataReg)
        .addReg(BaseReg)
        .addImm(Offset);
    BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), BaseReg)
        .addReg(BaseReg)
        .addImm(Stride);
    break;
  }
  case Haydn::LD64_POST_INC: {
    Register DstReg = MI.getOperand(0).getReg();
    Register BaseReg = MI.getOperand(1).getReg();
    int64_t Stride = MI.getOperand(2).getImm();
    int64_t Offset = MI.getOperand(3).getImm();
    // emit the canonical DB-named D_LDW_POST_IMM (d_ldw_post_imm) when
    // the displacement is zero, the stride is a multiple of the 8-byte access
    // width, and the scaled stride fits in signed imm6. This is the exact
    // analog of HiFi's ae_l64.ip and collapses the previous 2-instruction
    // LD64_S1 + ADDI32 split into one. Fall back to the split for non-zero
    // displacement, non-multiple stride, or out-of-range scaled index.
    // (emitted the parallel-namespace LD64_POST; retires that to the
    // DB name as the canonical form.)
    if (Offset == 0 && (Stride % 8) == 0 && isInt<6>(Stride >> 3)) {
      BuildMI(MBB, MI, DL, TII->get(Haydn::D_LDW_POST_IMM))
          .addReg(DstReg, RegState::Define)        // $rtd (loaded data, DR64)
          .addReg(BaseReg, RegState::Define)       // $rs_wb (AGU writeback)
          .addReg(BaseReg)                          // $rs (input base, tied)
          .addImm(Stride >> 3);                     // $scaled_imm (imm6 index)
      break;
    }
    // emit the plain LD64 (slot 0/1 choice-set) for the split post-inc
    // fallback so it can pack with a sibling load. The fused D_LDW_POST_IMM
    // path above is preferred (single instruction); this split is the rare
    // fallback for non-multiple stride / out-of-range index.
    BuildMI(MBB, MI, DL, TII->get(Haydn::LD64), DstReg)
        .addReg(BaseReg)
        .addImm(Offset);
    BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), BaseReg)
        .addReg(BaseReg)
        .addImm(Stride);
    break;
  }
  case Haydn::ST64_POST_INC: {
    Register DataReg = MI.getOperand(0).getReg();
    Register BaseReg = MI.getOperand(1).getReg();
    int64_t Stride = MI.getOperand(2).getImm();
    int64_t Offset = MI.getOperand(3).getImm();
    // emit the fused ST64_POST (D_SDW_POST_IMM semantics — store 64b rtd
    // to [rs], then rs += imm6<<3) when the displacement is zero, the stride
    // is a multiple of the 8-byte access width, and the scaled stride fits in
    // signed imm6 (-32..+31 elements). Mirrors the LD64_POST_INC fusion. Fall
    // back to the split for non-zero displacement, non-multiple stride, or
    // out-of-range scaled index.
    // td form: (outs GPR32:$rs_wb), (ins DR64:$rt, GPR32:$rs, simm6:$imm)
    // MCInst operand order: [rt, rs_wb, rs, scaled_imm]
    if (Offset == 0 && (Stride % 8) == 0 && isInt<6>(Stride >> 3)) {
      BuildMI(MBB, MI, DL, TII->get(Haydn::ST64_POST))
          .addReg(BaseReg, RegState::Define)       // $rs_wb (AGU writeback)
          .addReg(DataReg)                          // $rt (stored data, DR64)
          .addReg(BaseReg)                          // $rs (input base, tied)
          .addImm(Stride >> 3);                     // $scaled_imm (imm6 index)
      break;
    }
    BuildMI(MBB, MI, DL, TII->get(Haydn::ST64))
        .addReg(DataReg)
        .addReg(BaseReg)
        .addImm(Offset);
    BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), BaseReg)
        .addReg(BaseReg)
        .addImm(Stride);
    break;
  }
  default:
    llvm_unreachable("post-inc pseudo opcode check above missed a case");
  }

  MI.eraseFromParent();
  return true;
}
