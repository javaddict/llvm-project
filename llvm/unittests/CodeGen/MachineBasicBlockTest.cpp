//===- MachineBasicBlockTest.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {
// Include helper functions to ease the manipulation of MachineFunctions.
#include "MFCommon.inc"

TEST(FindDebugLocTest, DifferentIterators) {
  LLVMContext Ctx;
  Module Mod("Module", Ctx);
  auto MF = createMachineFunction(Ctx, Mod);
  auto &MBB = *MF->CreateMachineBasicBlock();

  // Create metadata: CU, subprogram, some blocks and an inline function
  // scope.
  DIBuilder DIB(Mod);
  DIFile *OurFile = DIB.createFile("foo.c", "/bar");
  DICompileUnit *OurCU = DIB.createCompileUnit(
      DISourceLanguageName(dwarf::DW_LANG_C99), OurFile, "", false, "", 0);
  auto OurSubT = DIB.createSubroutineType(DIB.getOrCreateTypeArray({}));
  DISubprogram *OurFunc =
      DIB.createFunction(OurCU, "bees", "", OurFile, 1, OurSubT, 1,
                         DINode::FlagZero, DISubprogram::SPFlagDefinition);

  DebugLoc DL0;
  DebugLoc DL1 = DILocation::get(Ctx, 1, 0, OurFunc);
  DebugLoc DL2 = DILocation::get(Ctx, 2, 0, OurFunc);
  DebugLoc DL3 = DILocation::get(Ctx, 3, 0, OurFunc);

  // Test using and empty MBB.
  EXPECT_EQ(DL0, MBB.findDebugLoc(MBB.instr_begin()));
  EXPECT_EQ(DL0, MBB.findDebugLoc(MBB.instr_end()));

  EXPECT_EQ(DL0, MBB.rfindDebugLoc(MBB.instr_rbegin()));
  EXPECT_EQ(DL0, MBB.rfindDebugLoc(MBB.instr_rend()));

  EXPECT_EQ(DL0, MBB.findPrevDebugLoc(MBB.instr_begin()));
  EXPECT_EQ(DL0, MBB.findPrevDebugLoc(MBB.instr_end()));

  EXPECT_EQ(DL0, MBB.rfindPrevDebugLoc(MBB.instr_rbegin()));
  EXPECT_EQ(DL0, MBB.rfindPrevDebugLoc(MBB.instr_rend()));

  // Insert two MIs with DebugLoc DL1 and DL3.
  // Also add a DBG_VALUE with a different DebugLoc in between.
  MCInstrDesc COPY = {TargetOpcode::COPY, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  MCInstrDesc DBG = {TargetOpcode::DBG_VALUE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  auto MI3 = MF->CreateMachineInstr(COPY, DL3);
  MBB.insert(MBB.begin(), MI3);
  auto MI2 = MF->CreateMachineInstr(DBG, DL2);
  MBB.insert(MBB.begin(), MI2);
  auto MI1 = MF->CreateMachineInstr(COPY, DL1);
  MBB.insert(MBB.begin(), MI1);

  // Test using two MIs with a debug instruction in between.
  EXPECT_EQ(DL1, MBB.findDebugLoc(MBB.instr_begin()));
  EXPECT_EQ(DL1, MBB.findDebugLoc(MI1));
  EXPECT_EQ(DL3, MBB.findDebugLoc(MI2));
  EXPECT_EQ(DL3, MBB.findDebugLoc(MI3));
  EXPECT_EQ(DL0, MBB.findDebugLoc(MBB.instr_end()));

  EXPECT_EQ(DL1, MBB.rfindDebugLoc(MBB.instr_rend()));
  EXPECT_EQ(DL1, MBB.rfindDebugLoc(MI1));
  EXPECT_EQ(DL3, MBB.rfindDebugLoc(MI2));
  EXPECT_EQ(DL3, MBB.rfindDebugLoc(MI3));
  EXPECT_EQ(DL3, MBB.rfindDebugLoc(MBB.instr_rbegin()));

  EXPECT_EQ(DL0, MBB.findPrevDebugLoc(MBB.instr_begin()));
  EXPECT_EQ(DL0, MBB.findPrevDebugLoc(MI1));
  EXPECT_EQ(DL1, MBB.findPrevDebugLoc(MI2));
  EXPECT_EQ(DL1, MBB.findPrevDebugLoc(MI3));
  EXPECT_EQ(DL3, MBB.findPrevDebugLoc(MBB.instr_end()));

  EXPECT_EQ(DL0, MBB.rfindPrevDebugLoc(MBB.instr_rend()));
  EXPECT_EQ(DL0, MBB.rfindPrevDebugLoc(MI1));
  EXPECT_EQ(DL1, MBB.rfindPrevDebugLoc(MI2));
  EXPECT_EQ(DL1, MBB.rfindPrevDebugLoc(MI3));
  EXPECT_EQ(DL1, MBB.rfindPrevDebugLoc(MBB.instr_rbegin()));

  // Finalize DIBuilder to avoid memory leaks.
  DIB.finalize();
}

// D1.115 / D1.40r: CreationID is a per-MF serial assigned only at
// CreateMachineBasicBlock. It must stay monotone across RenumberBlocks
// and MBB delete (recycler reuse cannot revive a deleted id). UniqueBBID
// remains gated off without BBAddrMap/list; printName still has no bb_id.
TEST(CreationIDTest, MonotoneAcrossRenumberAndDelete) {
  LLVMContext Ctx;
  Module Mod("Module", Ctx);
  auto MF = createMachineFunction(Ctx, Mod);

  MachineBasicBlock *A = MF->CreateMachineBasicBlock();
  MachineBasicBlock *B = MF->CreateMachineBasicBlock();
  MachineBasicBlock *C = MF->CreateMachineBasicBlock();
  MF->push_back(A);
  MF->push_back(B);
  MF->push_back(C);

  ASSERT_EQ(A->getCreationID(), 0u);
  ASSERT_EQ(B->getCreationID(), 1u);
  ASSERT_EQ(C->getCreationID(), 2u);
  ASSERT_EQ(MF->getMBBCreationHighWater(), 3u);
  ASSERT_FALSE(A->getBBID().has_value());
  ASSERT_FALSE(B->getBBID().has_value());
  ASSERT_FALSE(C->getBBID().has_value());

  const unsigned Epoch = MF->getBlockNumberEpoch();
  MF->RenumberBlocks();
  EXPECT_NE(MF->getBlockNumberEpoch(), Epoch);
  EXPECT_EQ(A->getCreationID(), 0u);
  EXPECT_EQ(B->getCreationID(), 1u);
  EXPECT_EQ(C->getCreationID(), 2u);
  EXPECT_EQ(MF->getMBBCreationHighWater(), 3u);
  EXPECT_EQ(A->getNumber(), 0);
  EXPECT_EQ(B->getNumber(), 1);
  EXPECT_EQ(C->getNumber(), 2);

  MF->erase(B->getIterator());
  EXPECT_EQ(A->getCreationID(), 0u);
  EXPECT_EQ(C->getCreationID(), 2u);
  EXPECT_EQ(MF->getMBBCreationHighWater(), 3u);

  MF->RenumberBlocks();
  EXPECT_EQ(A->getNumber(), 0);
  EXPECT_EQ(C->getNumber(), 1);
  EXPECT_EQ(A->getCreationID(), 0u);
  EXPECT_EQ(C->getCreationID(), 2u);
  EXPECT_NE(static_cast<unsigned>(C->getNumber()), C->getCreationID());
  EXPECT_EQ(MF->getMBBCreationHighWater(), 3u);

  MachineBasicBlock *D = MF->CreateMachineBasicBlock();
  MF->push_back(D);
  EXPECT_EQ(D->getCreationID(), 3u);
  EXPECT_EQ(MF->getMBBCreationHighWater(), 4u);
  EXPECT_FALSE(D->getBBID().has_value());

  std::string Name;
  raw_string_ostream OS(Name);
  A->printName(OS, MachineBasicBlock::PrintNameAttributes);
  EXPECT_EQ(Name.find("bb_id"), std::string::npos);
  EXPECT_EQ(Name.find("CreationID"), std::string::npos);
  EXPECT_EQ(Name.find("creation"), std::string::npos);
}

} // end namespace
