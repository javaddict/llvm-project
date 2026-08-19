; RUN: %python %S/../../../../utils/haydn/check_xfail_ledger.py --self-test
; RUN: %python %S/../../../../utils/haydn/check_xfail_ledger.py --inventory-pin --llvm-src %S/../../../../..
; RUN: %python %S/../../../../utils/haydn/check_xfail_ledger.py --runtime-pin --llvm-src %S/../../../../..
; RUN: %python %S/../../../../utils/haydn/check_xfail_ledger.py --llvm-src %S/../../../../..
; RUN: %python %S/../../../../utils/haydn/tdsp13_declared_vs_tested_pin.py --self-test
; RUN: %python %S/../../../../utils/haydn/tdsp13_declared_vs_tested_pin.py --llvm-src %S/../../../../..
; RUN: %python %S/../../../../utils/haydn/check_runtime_artifact_seats.py --self-test
; RUN: %python %S/../../../../utils/haydn/classify_lldb_step.py --self-test
; RUN: %python %S/../../../../utils/haydn/classify_lldb_step.py --json 0x10000 0x10000 | FileCheck %s --check-prefix=SAMEPC
; RUN: %python %S/../../../../utils/haydn/classify_lldb_step.py --json 0x10000 0x1000C | FileCheck %s --check-prefix=PARCEL
; RUN: %python %S/../../../../utils/haydn/parse_lit_summary.py --self-test
; RUN: FileCheck --input-file=%S/../../../../../lldb/source/Plugins/Instruction/Haydn/EmulateInstructionHaydn.cpp %s --check-prefix=STEP
; RUN: FileCheck --input-file=%S/../../../../../lldb/source/Plugins/ABI/Haydn/ABISysV_haydn.h %s --check-prefix=ABI
; RUN: FileCheck --input-file=%S/SOURCE-AUTHORITY-ANCHORS.txt %s --check-prefix=PIPE
; RUN: FileCheck --input-file=%S/SOURCE-AUTHORITY-ANCHORS.txt %s --check-prefix=ANCHORS
; RUN: FileCheck --input-file=%S/SOURCE-AUTHORITY-ANCHORS.txt %s --check-prefix=SCHEMA
; RUN: FileCheck --input-file=%S/FAULT-INJECTION-SEATS.txt %s --check-prefix=PIPE
; RUN: FileCheck --input-file=%S/CORRUPTION-MATRIX.txt %s --check-prefix=PIPE
; RUN: FileCheck --input-file=%S/CORRUPTION-MATRIX.txt %s --check-prefix=M5
; RUN: FileCheck --input-file=%S/../../../../lib/Target/Haydn/haydn-rt/PRODUCT-IDENTITY.txt %s --check-prefix=M5ID
; REQUIRES: haydn-registered-target

; Role: harness — T6-ARTIFACT compiler→sysroot→consumer inventory.
;
; Pins Inputs ledger/authority, haydn-rt M1 scale32/shift32 compile
; canaries past add16/add32, T-SF9 __addsf3 contract, T-DSP13 inventory,
; M10 parcel12
; Instruction plugin (no member decode; +12 only when PC is unchanged),
; PIPE-20/DG0 inventory-only leftover, M15/M16 leftover freeze,
; and the Failed≠XFAIL parser.
; Does not run BundleSim ctest, gcc-torture, NatureDSP execution, or a
; libc sysroot rebuild. vec_dot16 exactness stays residual.

; STEP: kFormatEParcelBytes
; STEP: when PC is unchanged
; STEP: No member decode
; STEP: EncodedBytes=12
; STEP-NOT: GetOpcodeForInstruction

; ABI: kStepNeverQualified = false
; ABI: ClassifyStepReport
; ABI: ReturnRegNameForBytes
; SAMEPC: "class": "same-pc-residual"
; SAMEPC: "qualified": false
; PARCEL: "class": "parcel12"
; PARCEL: "qualified": false

; PIPE: PIPE-20
; PIPE: inventory
; PIPE: DecisionGuard product registry
; PIPE: absent
; PIPE: T-DSP13
; PIPE: declared-vs-tested
; ANCHORS-DAG: T8-ALIGN-POLICY
; ANCHORS-DAG: T8-DEBUG-EVIDENCE
; ANCHORS-DAG: T8-EVID
; ANCHORS-DAG: never qualified
; ANCHORS-DAG: 9e5c878a
; ANCHORS-DAG: 28700d57
; ANCHORS-DAG: G-ECOSYSTEM-CONSUMERS
; ANCHORS-DAG: G-LIBRARY-COVERAGE
; ANCHORS-DAG: G-DEBUG-OBSERVABILITY
; ANCHORS-DAG: G-TEST-EVIDENCE
; ANCHORS-DAG: not an ancestor
; ANCHORS-DAG: G_ANYEXT
; ANCHORS-DAG: adjustsStack
; ANCHORS-DAG: MaxParcels
; ANCHORS-DAG: T-TI3 per-pass coverage
; ANCHORS-DAG: T-TI4 MC/disasm fuzzer
; ANCHORS-DAG: T-TI5 stored perf baselines
; ANCHORS-DAG: T-TI6 randomized codegen
; ANCHORS-DAG: do not invent a fuzz gate
; ANCHORS-DAG: TOTAL=5
; ANCHORS-DAG: nine-file
; ANCHORS-DAG: D1000
; ANCHORS-DAG: ClassifyStepReport
; ANCHORS-DAG: kStepNeverQualified
; SCHEMA-DAG: PIPE-30
; SCHEMA-DAG: schema surface closed
; SCHEMA-DAG: callback-local
; SCHEMA-DAG: T4/T3
; SCHEMA-DAG: no second format pipeline
; SCHEMA-DAG: T2+T4
; SCHEMA-DAG: MF0
; SCHEMA-DAG: INERT
; SCHEMA-DAG: ff42d9ad
; M5: T-ABI4 R0 ISR invent
; M5: T-ABI6 i128 invent
; M5: T-ABI11 builtin invent
; M5: T-ABI12 inline-asm invent
; M5ID: T-ABI4 R0 invariant
; M5ID: T-ABI6 i128
; M5ID: T-ABI11 remaining declared-legal
; M5ID: T-ABI12 inline-asm
