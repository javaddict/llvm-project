//===-- HaydnCircularBuffer.cpp - Circular Buffer Detection ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a post-register-allocation pass that detects circular
// buffer access patterns for the Haydn VLIW DSP target.
//
// A circular buffer in C typically looks like:
//
// int buf[N]; / N is a power of 2
// int idx = 0;
// in a loop:
// val = buf[idx];
// idx = (idx + 1) & (N - 1); / wrap-around mask
//
// In compiled code, this manifests as:
//
// ANDI32 rIdx, rIdx, Mask; Mask = N-1 = 2^k - 1 (power of 2 minus 1)
// SLLI32 rOff, rIdx, 2; scale index by element size (for i32)
// ADD32 rAddr, rBase, rOff; compute address
// LD32 rVal, rAddr, 0; load from circular buffer
//
// OR, when the AND result is used directly as an offset (byte addressing):
//
// ANDI32 rIdx, rIdx, Mask
// LD32 rVal, rBase, rIdx; (if the ISA supported register-offset loads)
//
// This pass detects both patterns and counts statistics. It runs after
// register allocation (NoVRegs required) so that physical register tracking
// is stable.
//
// Detection strategy:
// 1. Scan for ANDI32 or AND32 instructions where the immediate operand
// (or a register holding a constant) is of the form 2^k - 1.
// 2. Track the result register forward through a limited chain of
// address computations (shift, add) to see if it reaches a load or
// store instruction.
// 3. If the AND result (or a simple derivative) is used as part of a
// memory address, record it as a circular buffer access.
//
//===----------------------------------------------------------------------===//

#include "HaydnCircularBuffer.h"
#include "Haydn.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "haydn-circular-buffer"

using namespace llvm;

STATISTIC(NumCircularBufferPatterns,
          "Number of circular buffer access patterns detected");
STATISTIC(NumCircularBufferMasks,
          "Number of AND instructions with circular buffer masks (2^k - 1)");

//===----------------------------------------------------------------------===//
// Helper: check if a value is of the form 2^k - 1 (all ones in binary)
//===----------------------------------------------------------------------===//

// Returns true if \p Val is of the form 2^k - 1 for some k >= 1.
// These are numbers whose binary representation is all ones: 1, 3, 7, 15
// 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191, 16383, 32767, 65535.
static bool isPowerOf2MinusOne(uint64_t Val) {
  if (Val == 0)
    return false;
  return (Val & (Val + 1)) == 0;
}

//===----------------------------------------------------------------------===//
// Helper: track register forward to find if it reaches a load/store
//===----------------------------------------------------------------------===//

// Scan forward from \p StartMI in \p MBB, looking for load/store
// instructions that use \p Reg (possibly through simple copy chains).
// Returns true if the register reaches a memory operation within a limited
// window of instructions.
static bool registerReachesLoadStore(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator StartMI,
                                     Register Reg, unsigned WindowSize = 12) {
  DenseSet<Register> TrackedRegs;
  TrackedRegs.insert(Reg);

  unsigned Count = 0;
  for (auto It = StartMI; It != MBB.end() && Count < WindowSize; ++It, ++Count) {
    MachineInstr &MI = *It;

    // Check if any tracked register is used by this instruction.
    bool UsesTrackedReg = false;
    for (const MachineOperand &MO : MI.all_uses()) {
      if (MO.isReg() && TrackedRegs.count(MO.getReg())) {
        UsesTrackedReg = true;
        break;
      }
    }

    if (!UsesTrackedReg)
      continue;

    // If the instruction is a load or store, the pattern is confirmed.
    if (MI.mayLoad() || MI.mayStore()) {
      // Verify the tracked register contributes to the address (not the data).
      // For loads: address operands are inputs; data register is output.
      // For stores: data and address are both inputs.
      // Heuristic: if a tracked register is used and the instruction is a
      // memory operation, count it. In practice, the AND result feeds into
      // address computation (ADD32), which then feeds into LD32/ST32 base.
      return true;
    }

    // Track the result through COPY instructions.
    if (MI.isCopy()) {
      Register DstReg = MI.getOperand(0).getReg();
      if (DstReg.isPhysical())
        TrackedRegs.insert(DstReg);
      continue;
    }

    // Track through simple address computation instructions:
    // ADD32, ADDI32, SLLI32, SRLI32, SLL32, SRL32 (scaled array addressing:
    // addr = idx * scale + base). The result is a new tracked register.
    // MAC32 removed (not in the ISA DB).
    unsigned Opc = MI.getOpcode();
    if (Opc == Haydn::ADD32 || Opc == Haydn::ADDI32 ||
        Opc == Haydn::ADDI32_W ||
        Opc == Haydn::SLLI32 || Opc == Haydn::SRLI32 ||
        Opc == Haydn::SLL32 || Opc == Haydn::SRL32 ||
        Opc == Haydn::SUB32 || Opc == Haydn::SUBI32) {
      if (MI.getOperand(0).isReg() && MI.getOperand(0).getReg().isPhysical())
        TrackedRegs.insert(MI.getOperand(0).getReg());
    }
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Helper: try to extract a constant value from a register def
//===----------------------------------------------------------------------===//

// For AND32 (register-register form), try to determine if the second source
// operand is a constant. Look backward for a LI32/LUI+ORI32 constant
// materialization sequence.
static bool tryGetConstantFromRegDef(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator BeforeMI,
                                     Register Reg, uint64_t &Val) {
  // Scan backward up to 4 instructions looking for the definition of Reg.
  if (BeforeMI == MBB.begin())
    return false;
  unsigned Count = 0;
  auto It = std::prev(BeforeMI);
  for (;;) {
    if (Count >= 4)
      return false;
    ++Count;

    MachineInstr &MI = *It;

    // Check if this instruction defines Reg.
    bool DefinesReg = false;
    for (const MachineOperand &MO : MI.all_defs()) {
      if (MO.isReg() && MO.getReg() == Reg) {
        DefinesReg = true;
        break;
      }
    }

    if (!DefinesReg) {
      // Move backward; stop if we hit the beginning.
      if (It == MBB.begin())
        return false;
      --It;
      continue;
    }

    unsigned Opc = MI.getOpcode();

    // LUI: imm12 into bits[31:20] (ISA: rt = {imm12, 20'b0})..
    if (Opc == Haydn::LUI && MI.getOperand(2).isImm()) {
      Val = (static_cast<uint64_t>(MI.getOperand(2).getImm()) & 0xFFFull) << 20;
      return true;
    }

    // ADDI32 with R0 source: effectively loading an immediate.
    // addi32 rd, r0, imm -> rd = imm
    if ((Opc == Haydn::ADDI32 || Opc == Haydn::ADDI32_W) &&
        MI.getOperand(1).isReg() &&
        MI.getOperand(1).getReg() == Haydn::R0 && MI.getOperand(2).isImm()) {
      Val = static_cast<uint64_t>(MI.getOperand(2).getImm());
      return true;
    }

    // ORI32: or with immediate. If source is from LUI, this is LO16.
    // For now, handle the simple ORI32 with R0 case: ori32 rd, r0, imm
    if (Opc == Haydn::ORI32 && MI.getOperand(1).isReg() &&
        MI.getOperand(1).getReg() == Haydn::R0 && MI.getOperand(2).isImm()) {
      Val = static_cast<uint64_t>(MI.getOperand(2).getImm());
      return true;
    }

    // COPY from another register — follow the chain.
    if (MI.isCopy()) {
      Reg = MI.getOperand(1).getReg();
      if (It == MBB.begin())
        return false;
      --It;
      continue;
    }

    // Unknown definition pattern.
    return false;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// Public interface
//===----------------------------------------------------------------------===//

char HaydnCircularBuffer::ID = 0;

INITIALIZE_PASS(HaydnCircularBuffer, "haydn-circular-buffer",
                "Haydn Circular Buffer Detection", false, false)

FunctionPass *llvm::createHaydnCircularBufferPass() {
  return new HaydnCircularBuffer();
}

HaydnCircularBuffer::HaydnCircularBuffer()
    : MachineFunctionPass(ID) {}

void HaydnCircularBuffer::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool HaydnCircularBuffer::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  // Only run the analysis when the target actually has circular-buffer
  // addressing (CBR0-CBR3). The `generic` CPU leaves FeatureCircularBuffer
  // off (Haydn.td:77); only `-mcpu=haydn` or `-mattr=+circular-buffer`
  // enables it. Without CBR registers there is nothing to detect.
  const HaydnSubtarget &STI = MF.getSubtarget<HaydnSubtarget>();
  if (!STI.hasCircularBuffer())
    return false;

  LLVM_DEBUG(dbgs() << "===== Haydn Circular Buffer Detection: "
                    << MF.getName() << " =====\n");

  unsigned PatternsFound = 0;
  unsigned MasksFound = 0;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      uint64_t MaskVal = 0;
      Register ResultReg;
      bool FoundMask = false;

      unsigned Opc = MI.getOpcode();

      // Pattern 1: ANDI32 rd, rs, imm where imm is 2^k - 1
      if (Opc == Haydn::ANDI32) {
        // Operand layout: rd (def), rs (use), imm
        if (MI.getOperand(2).isImm()) {
          uint64_t Imm = static_cast<uint64_t>(MI.getOperand(2).getImm());
          if (isPowerOf2MinusOne(Imm)) {
            ResultReg = MI.getOperand(0).getReg();
            MaskVal = Imm;
            FoundMask = true;
          }
        }
      }
      // Pattern 2: AND32 rd, rs1, rs2 where rs2 is a constant 2^k - 1
      else if (Opc == Haydn::AND32) {
        // Operand layout: rd (def), rs1 (use), rs2 (use)
        Register Rs2 = MI.getOperand(2).getReg();
        uint64_t Val = 0;
        if (tryGetConstantFromRegDef(MBB, MI.getIterator(), Rs2, Val) &&
            isPowerOf2MinusOne(Val)) {
          ResultReg = MI.getOperand(0).getReg();
          MaskVal = Val;
          FoundMask = true;
        }
      }

      if (!FoundMask)
        continue;

      ++MasksFound;
      LLVM_DEBUG({
        dbgs() << "  Circular buffer mask found: AND";
        if (Opc == Haydn::ANDI32)
          dbgs() << "I32";
        else
          dbgs() << "32";
        dbgs() << " with mask=" << MaskVal << " (buffer size="
               << (MaskVal + 1) << ")\n";
        dbgs() << "    " << MI;
      });

      // Check if the result register flows into a load/store.
      auto NextIt = std::next(MI.getIterator());
      if (NextIt != MBB.end() &&
          registerReachesLoadStore(MBB, NextIt, ResultReg)) {
        ++PatternsFound;
        LLVM_DEBUG(dbgs() << "    -> reaches load/store: circular buffer "
                          << "access confirmed (size=" << (MaskVal + 1)
                          << ")\n");
      }
    }
  }

  NumCircularBufferMasks += MasksFound;
  NumCircularBufferPatterns += PatternsFound;

  if (PatternsFound > 0) {
    LLVM_DEBUG(dbgs() << "  Total circular buffer patterns in "
                      << MF.getName() << ": " << PatternsFound << "\n");
  }

  // This pass is analysis-only: no modifications to the function.
  return false;
}
