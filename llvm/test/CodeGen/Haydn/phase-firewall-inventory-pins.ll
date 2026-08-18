; RUN: FileCheck %s --input-file=%S/Inputs/SOURCE-AUTHORITY-ANCHORS.txt --check-prefix=PIPE20
; RUN: FileCheck %s --input-file=%S/Inputs/SOURCE-AUTHORITY-ANCHORS.txt --check-prefix=DG0
; RUN: FileCheck %s --input-file=%S/Inputs/SOURCE-AUTHORITY-ANCHORS.txt --check-prefix=REBIND
; RUN: FileCheck %s --input-file=%S/Inputs/FAULT-INJECTION-SEATS.txt --check-prefix=FAULT
; RUN: FileCheck %s --input-file=%S/Inputs/CORRUPTION-MATRIX.txt --check-prefix=CORR
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfoAuto.td --check-prefix=AUTO
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/CMakeLists.txt --check-prefix=CMAKE
; RUN: FileCheck %s --input-file=%S/../../../utils/haydn/product_coverage_pin.sh --check-prefix=PIN
; RUN: FileCheck %s --input-file=%S/../../../../CLAUDE.md --check-prefix=ISANEXT
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnBundlePlan.h --check-prefix=SCHEMA
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnBundle.h --check-prefix=BUNDLE
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnAlternateDescriptors.h --check-prefix=ALT
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnPlacementAlternative.h --check-prefix=PLACE
; RUN: test -f %S/../../../utils/haydn/product_coverage_pin.sh
; RUN: git -C %S/../../../.. merge-base --is-ancestor 38bd4059fbb4e489425becc4ded431235ae2c1ff HEAD
; RUN: not git -C %S/../../../.. merge-base --is-ancestor 28700d57366a35a7d04e8adfbdf782743ec847e0 HEAD
; RUN: %python %S/../../../utils/haydn/parse_lit_summary.py --self-test
; RUN: %python %S/../../../utils/haydn/check_xfail_ledger.py --inventory-pin --llvm-src %S/../../../..
; RUN: %python -c "import os,sys; p=sys.argv[1]; assert os.path.islink(p), p+' must remain a symlink to haydn-plans/llvm-claude.md, not a 56K duplicate'" %S/../../../../CLAUDE.md
; REQUIRES: haydn-registered-target

; Role: harness — PIPE-20 / DG0 / P19 / R15 / T7-ALIGN Goal 1 ledger + W55 inventory pins only.
; Inputs/ is a lit-excluded data dir; this file is the runnable owner seat.
; DecisionGuard product registry stays absent. SMS/hwloop defaults stay OFF.
; Auto.td is hand-maintained. CMake golden-dir is env/-D only; skip if unset.
; Monorepo CLAUDE.md stays a symlink (R15 leftover docs stay outside this
; code track). Rebound: 38bd4059 is an ancestor; 28700d57 is not an ancestor.
; AR0 phase-firewall inventory is closed (no pre-RA identity).

; PIPE20-DAG: Phase-firewall inventory (PIPE-20
; PIPE20-DAG: no issue-cycle/format identity crosses RA
; PIPE20-DAG: no pre-RA BUNDLE / private member / setDesc
; PIPE20-DAG: AR0 phase-firewall inventory closed
; PIPE20-DAG: leftover residual-pseudo matrix
; PIPE20-DAG: inventory only
; PIPE20-DAG: T8-EVID

; DG0-DAG: DecisionGuard product registry remains absent
; DG0-DAG: no G-DECISION-GUARD revive
; DG0-DAG: product_coverage_pin
; DG0-NOT: DecisionGuardRegistry

; REBIND-DAG: 38bd4059
; REBIND-DAG: 28700d57
; REBIND-DAG: not an ancestor
; REBIND-DAG: G_ANYEXT
; REBIND-DAG: adjustsStack
; REBIND-DAG: MaxParcels
; REBIND-DAG: Late Finalize/Verify after BR
; REBIND-DAG: P19 CMake leftover closed
; REBIND-DAG: ISA-64
; REBIND-DAG: pass-deep-review-2026-08-14
; REBIND-DAG: P13 source waves
; REBIND-DAG: do not start Wave 1/2/3
; REBIND-DAG: Goal 1 ledger
; REBIND-DAG: W55
; REBIND-DAG: nine-file
; REBIND-DAG: T-TI3 per-pass coverage
; REBIND-DAG: T-TI4 MC/disasm fuzzer
; REBIND-DAG: T-TI5 stored perf baselines
; REBIND-DAG: T-TI6 randomized codegen
; REBIND-DAG: do not invent a fuzz gate
; REBIND-DAG: TOTAL=5
; REBIND-DAG: T7-ALIGN schema tip-reconcile
; REBIND-DAG: no empty-cover / cross-row
; REBIND-DAG: no suffix discovery

; FAULT-DAG: AR0 leftovers are inventory, not a product registry
; FAULT-DAG: AR0 phase-firewall inventory closed
; FAULT-DAG: DecisionGuard registry stays absent
; FAULT-DAG: check_xfail_ledger.py
; FAULT-DAG: product_coverage_pin
; FAULT-DAG: 28700d57
; FAULT-DAG: vf5-residual-set-hwloop-ban.mir
; FAULT-DAG: vf5-residual-setcbr-ban.mir
; FAULT-DAG: vf5-residual-loopctl-ban.mir
; FAULT-DAG: vf5-residual-load-addr-ban.mir
; FAULT-DAG: vf5-residual-bundled-child-ban.mir
; FAULT-DAG: T-TI3 per-pass coverage
; FAULT-DAG: T-TI4 MC/disasm fuzzer
; FAULT-DAG: T-TI5 stored perf baselines
; FAULT-DAG: T-TI6 randomized codegen

; CORR-DAG: AR0 leftovers are inventory, not a product registry
; CORR-DAG: AR0 phase-firewall inventory closed
; CORR-DAG: DecisionGuard registry stays absent
; CORR-DAG: no host / no force-fail invent
; CORR-DAG: 28700d57
; CORR-DAG: vf5-residual-cycle-pseudo-ban.mir
; CORR-DAG: vf5-residual-set-hwloop-ban.mir
; CORR-DAG: vf5-residual-setcbr-ban.mir
; CORR-DAG: vf5-residual-loopctl-ban.mir
; CORR-DAG: vf5-residual-load-addr-ban.mir
; CORR-DAG: vf5-residual-bundled-child-ban.mir
; CORR-DAG: T-TI3 coverage invent
; CORR-DAG: T-TI4 fuzzer invent
; CORR-DAG: T-TI5 baseline invent
; CORR-DAG: T-TI6 randomized invent

; PIN-DAG: 28700d57
; PIN-DAG: merge-base --is-ancestor

; AUTO: hand-maintained hypothesized encodings
; AUTO: generate_format_e_records.py does not emit this file
; AUTO-NOT: Auto-generated from spec JSON

; CMAKE: Never fall back to a host-absolute plans-tree path
; CMAKE: HAYDN_GOLDEN_DIR
; CMAKE: BUNDLESIM_GOLDEN_DIR
; CMAKE: skip if unset
; CMAKE-NOT: /ssd2/mhyang/haydn-plans/Database/golden
; CMAKE-NOT: $ENV{HOME}/haydn

; Highest existing ISA file is ISA-63; the next new file is ISA-64.
; ISANEXT: next new file is `ISA-64`

; Closed T1-ALIGN schema: PacketFormats full-cover only (AIEBundle.h:150-156).
; SCHEMA: no empty-cover first-match and no E2
; SCHEMA: return nullptr
; BUNDLE: Never stamp the empty-cover product representative
; ALT: Suffix name discovery is not
; ALT: a product alternate source
; PLACE: Suffix `_S*` spelling is occupancy recovery, not a
; PLACE: product alternate source
