; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.h --check-prefix=HWDEF
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp --check-prefix=HWFLAG
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp --check-prefix=LATE
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnSubtarget.h --check-prefix=O0POST
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnEnsureTerminators.cpp --check-prefix=ENSURE
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/CMakeLists.txt --check-prefix=CMAKE
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfoManual.td --check-prefix=AUTO
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnFeatures.td --check-prefix=FEAT
;
; Role: semantic — T7-SOURCE-ALIGN owner-slice pins. Single AIE lifecycle
; ledger in TargetMachine; hwloop product default flipped ON 2026-08-22
; (qualified independent + combined; see HaydnTargetMachine.h); P19 CMake
; is env/-D only; Auto.td is hand-maintained via Generic include;
; EnsureTerminators never skipFunction. P13 source waves stay gated while
; `_S*` FieldSlot defs remain.

; HWDEF: hardwareLoopsProductDefaultEnabled() { return true; }
; HWFLAG-DAG: static_assert(HaydnTargetMachine::hardwareLoopsProductDefaultEnabled()
; HWFLAG-DAG: "haydn-enable-hwloops"
; HWFLAG-DAG: cl::init(HaydnTargetMachine::hardwareLoopsProductDefaultEnabled())
; LATE: Mid Finalize+Verify at every opt level (identity on already-bundled
; LATE-NOT: Product default does not pay a second Finalize+Verify
; O0POST-DAG: enablePostRAMachineScheduler() const override { return true; }
; O0POST-DAG: no BUNDLE, private member, row
; ENSURE: never call skipFunction
; CMAKE: regeneration is an explicit developer step
; CMAKE-NOT: HAYDN_GOLDEN_DIR
; CMAKE-NOT: BUNDLESIM_GOLDEN_DIR
; CMAKE-NOT: HaydnFormatERecordsCheck
; CMAKE-NOT: /ssd2/mhyang/haydn-plans/Database/golden
; CMAKE-NOT: $ENV{HOME}/haydn
; AUTO: hand-maintained hypothesized encodings
; AUTO: generate_format_e_records.py does not emit this file
; FEAT: Does not enable HardwareLoops formation
; FEAT: hardwareLoopsProductDefaultEnabled()
