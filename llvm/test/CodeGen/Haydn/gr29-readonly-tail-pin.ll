; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnFinalizeBundle.h --check-prefix=FINH
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnFinalizeBundle.cpp --check-prefix=FINC
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnVerifyBundles.h --check-prefix=VERH
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnVerifyBundles.cpp --check-prefix=VERC
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnAsmPrinter.cpp --check-prefix=ASM
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnAsmPrinter.h --check-prefix=ASMH
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp --check-prefix=TM
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnMachineAlignment.h --check-prefix=ALIGN
; RUN: %python -c "import pathlib,sys; t=pathlib.Path(sys.argv[1]).read_text(); assert 'createHaydnMachineAlignmentPass' not in t" %S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp
; RUN: %python -c "import pathlib,sys; t=pathlib.Path(sys.argv[1]).read_text(); assert 'createHaydnMachineAlignmentPass' not in t; assert 'class HaydnMachineAlignment' not in t" %S/../../../lib/Target/Haydn/HaydnMachineAlignment.h
; RUN: %python -c "import pathlib,sys; t=pathlib.Path(sys.argv[1]).read_text(); assert 'createHaydnMachineAlignmentPass' not in t; assert 'class HaydnMachineAlignment : public MachineFunctionPass' not in t" %S/../../../lib/Target/Haydn/HaydnMachineAlignment.cpp
;
; GR2.9: shared LBN closer + padInternalMBBAlignment ClearMetadata=true
; finish in addPreEmitPass before the common tail. addPostBBSections is
; the empty TPC default (TargetPassConfig.h:447). MachineAlignment pass
; class is greppably absent (library stays). Finalize/Verify/AsmPrinter
; must not name addPostBBSections as the closure seat.
; Peer: AIE seats createAIEMachineAlignment in addPreSched2 only because
; AIE PreEmit is empty (AIE2TargetMachine.cpp:238-255). Hexagon has no
; addPostBBSections override. Common TPC default is empty; do not copy
; AArch64 late writers (AArch64TargetMachine.cpp:883-895).

; FINH: addPreEmit after LBN closer
; FINH: addPostBBSections empty
; FINH-NOT: addPostBBSections closure
; FINH-NOT: closure at addPostBBSections

; FINC: addPreEmit after LBN closer
; FINC: addPostBBSections is empty
; FINC-NOT: addPostBBSections closure
; FINC-NOT: closure at addPostBBSections

; VERH: two earlier IsFreezeSeat=false
; VERH: two earlier Verify seats pass false
; VERH-NOT: three earlier
; VERH-NOT: addPostBBSections closure

; VERC: two intermediate Verify seats
; VERC: last wrap-only Finalize is addPreEmit
; VERC: addPostBBSections is empty
; VERC-NOT: closure Finalize at addPostBBSections
; VERC-NOT: three intermediate Verify seats

; ASM: HasFunctionAlignment=true
; ASM: padInternalMBBAlignment in stamped addPreEmit LBN
; ASM: addPostBBSections is empty
; ASM-NOT: HaydnMachineAlignment
; ASM-NOT: addPostBBSections, after the closure
; ASM-NOT: HasFunctionAlignment=false

; ASMH: HasFunctionAlignment=true
; ASMH: addPostBBSections is empty
; ASMH-NOT: HasFunctionAlignment=false

; TM: void HaydnPassConfig::addPostBBSections()
; TM-NOT: addPass(
; TM: void HaydnPassConfig::addPreEmitPass2()

; ALIGN: Library, not a MachineFunctionPass
; ALIGN: addPreEmitPass
; ALIGN-NOT: class HaydnMachineAlignment
; ALIGN-NOT: createHaydnMachineAlignmentPass
