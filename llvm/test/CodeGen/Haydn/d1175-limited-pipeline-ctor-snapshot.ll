; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp --check-prefix=SNAP
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp --check-prefix=SKIP
; RUN: FileCheck %s --input-file=%S/../../../include/llvm/CodeGen/TargetPassConfig.h --check-prefix=STATIC
;
; D1.175: freeze skip is keyed on truncation captured in the
; HaydnPassConfig ctor, not a later re-read of process-global
; StartBefore/StartAfter/StopBefore/StopAfter cl::opts. Do not promote
; TargetPassConfig::hasLimitedCodeGenPipeline to an instance method
; (that would be a 27th GR2.0 file). addPostBBSections is empty (GR2.9);
; the snapshot is used only for the addPreEmitPass2 freeze skip.
; d113 pins the product freeze census; this file does not add a second
; IsFreezeSeat assert.
;
; SNAP: LimitedCodeGenPipeline(TargetPassConfig::hasLimitedCodeGenPipeline())
; SNAP: void HaydnPassConfig::addPreEmitPass2()
; SNAP-NOT: hasLimitedCodeGenPipeline(
; SNAP-NOT: StartBefore
; SNAP-NOT: StartAfter
; SNAP-NOT: StopBefore
; SNAP-NOT: StopAfter
; SNAP: if (LimitedCodeGenPipeline)
; SNAP-NEXT: return;
;
; STATIC: static bool hasLimitedCodeGenPipeline();
;
; SKIP: void HaydnPassConfig::addPostBBSections()
; SKIP-NOT: addPass(
; SKIP: void HaydnPassConfig::addPreEmitPass2()
; SKIP: if (LimitedCodeGenPipeline)
; SKIP-NEXT: return;
; SKIP-NEXT: addPass(createHaydnVerifyBundlesPass(/*IsFreezeSeat=*/true));
