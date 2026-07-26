//===- HaydnFinalizeBundle.cpp ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Port of AIEFinalizeBundle (AIEFinalizeBundle.cpp:22-54 isBundleCandidate +
// runOnMachineFunction loop). Haydn delta vs AIE:
//
//   * After finalizeBundle, stamp FormatID::Bundle128Full as the BUNDLE-root
//     imm (B1.1 / plan §6.3; AIEHazardRecognizer.cpp:278-312 peer for multi-MI;
//     this pass covers singletons).
//   * B4.3 late layout firewall: before wrap, materialize bare multi-slot
//     logicals via empty-cycle tryAdd → setDesc(member)
//     (AIEMachineScheduler.cpp:1121-1139 materializeMultiOpcodeInstrs;
//     AIEHazardRecognizer.cpp:174-214 alt try; HaydnBundleMaterialize
//     commitLateProductCycle). Idempotent on already-bundled / already-
//     setDesc members. Product encode remains BUNDLE128_FULL only.
//
// Pipeline:
//   * addPreSched2 after PostMachineScheduler (AIE2TargetMachine.cpp:242-244)
//   * addPreEmit after BR→FixupHwLoops→BR growth (Haydn-only; AIE PreEmit
//     empty AIE2TargetMachine.cpp:88 / AIEBaseTargetMachine.cpp:388)
//
//===----------------------------------------------------------------------===//

#include "HaydnFinalizeBundle.h"
#include "Haydn.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundlePlan.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-finalize-mi-bundles"

namespace {

// Port of AIEFinalizeBundle.cpp:22-36 isBundleCandidate.
// AIE also skips isHardwareLoopEnd; Haydn PseudoLoopEnd is already
// isMetaInstruction (skipped below). LoopDec/LoopJNZ are real and wrap.
bool isBundleCandidate(MachineBasicBlock::instr_iterator MII) {
  MachineInstr *MI = &*MII;
  // Meta / debug / CFI / lifetime / KILL / ImplicitDef — not encode cycles.
  // Already-bundled MIs (BUNDLE roots with BundledSucc, or children).
  if (MI->isMetaInstruction() || MII->isBundled())
    return false;
  // Inline asm is a TargetOpcode pseudo lowered by AsmPrinter::emitInlineAsm
  // on the top-level MI. Wrapping it as a BUNDLE child drops the APP block
  // (BUNDLE path skips non-SETCBR pseudos). Leave standalone.
  if (MI->isInlineAsm())
    return false;
  return true;
}

/// B4.3: setDesc bare multi-slot logical to empty-cycle tryAdd member before
/// finalizeBundle. Port of AIE materializeMultiOpcodeInstrs setDesc
/// (AIEMachineScheduler.cpp:1126-1132) on a late singleton cycle
/// (HaydnBundleMaterialize.h commitLateProductCycle).
bool materializeLateBareIfNeeded(MachineInstr &MI, const TargetInstrInfo &TII,
                                 HaydnMCFormats &Fmts) {
  auto Cycle = haydn::bundle::commitLateProductCycle(MI.getOpcode(), Fmts);
  if (!Cycle || !Cycle->NeedsSetDesc)
    return false;
  MI.setDesc(TII.get(Cycle->MemberOpcode));
  LLVM_DEBUG(dbgs() << "HaydnFinalizeBundle: late setDesc "
                    << Cycle->LogicalOpcode << " → " << Cycle->MemberOpcode
                    << " (empty-cycle tryAdd; AIE materializeMultiOpcodeInstrs "
                       "peer)\n");
  return true;
}

} // namespace

bool HaydnFinalizeBundle::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  HaydnMCFormats Fmts;

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::instr_iterator MII = MBB.instr_begin();
    MachineBasicBlock::instr_iterator MIE = MBB.instr_end();
    if (MII == MIE)
      continue;
    assert(!MII->isInsideBundle() && "First instr cannot be inside bundle!");

    // Port of AIEFinalizeBundle.cpp:49-56: wrap each standalone candidate.
    // B4.3: setDesc first when empty-cycle tryAdd selects a format member
    // (late bare NOP/BR/demote inserts after PreEmit growth).
    while (MII != MIE) {
      if (!MII->isInsideBundle() && isBundleCandidate(MII)) {
        if (materializeLateBareIfNeeded(*MII, TII, Fmts))
          Changed = true;
        finalizeBundle(MBB, MII, std::next(MII));
        // MII still points at the original MI (now a BUNDLE child).
        MachineInstr &Root = *getBundleStart(MII);
        assert(Root.isBundle() && "finalizeBundle must produce a BUNDLE root");
        // Durable FormatID (HaydnBundlePlan.h stampBundleFormatID; multi-MI
        // path: HaydnPostRASchedStrategy.cpp:266-280).
        haydn::bundle::stampBundleFormatID(Root,
                                           haydn::bundle::ProductFormatID);
        Changed = true;
      }
      ++MII;
    }
  }

  return Changed;
}

char HaydnFinalizeBundle::ID = 0;

INITIALIZE_PASS(HaydnFinalizeBundle, DEBUG_TYPE, "Haydn Bundle Finalization",
                false, false)

HaydnFinalizeBundle::HaydnFinalizeBundle() : MachineFunctionPass(ID) {
  initializeHaydnFinalizeBundlePass(*PassRegistry::getPassRegistry());
}

void HaydnFinalizeBundle::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

FunctionPass *llvm::createHaydnFinalizeBundlePass() {
  return new HaydnFinalizeBundle();
}
