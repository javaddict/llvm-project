; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnBundle.h --check-prefix=SLOTMAP
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnBundlePlan.h --check-prefix=IDLE
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnPackLegality.h --check-prefix=CAP
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnPortModel.h --check-prefix=PORT
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnSchedule.td --check-prefix=MODEL
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnPostRASchedStrategy.cpp --check-prefix=SEQ
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnResourceCycle.h --check-prefix=SFR
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfo.cpp --check-prefix=R0
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnBundleMaterialize.cpp --check-prefix=TXN
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnMachineScheduler.cpp --check-prefix=IB
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnSchedMutations.cpp --check-prefix=IBBRICK
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnSchedMutations.h --check-prefix=IBHDR
; REQUIRES: haydn-registered-target
;
; Role: IR-less owner pins for ordinary post-RA host + shared resource model.
; Idle-parcel provenance stays OPEN_BLOCKED (no invent). Solo-class
; enum + generated E2=2/E3=3 + per-field MOVE32 + SFR 2R/1W + CompleteModel=0
; + sequentialize-is-not-legality. Memory-object wait stays unadmitted.
; SlotMap stays a suffix of CycleMember history on mixed generated+logical
; commit (hwloop-ON).

; SLOTMAP: appendCommittedMember
; SLOTMAP: issueFieldSlotsForCommittedKind
; SLOTMAP-NOT: rebuild a singleton
; SLOTMAP-NOT: member history not required

; IDLE: POS_IDLE_CANONICAL
; IDLE: OPEN_BLOCKED
; IDLE: haydnPosIdleCanonicalAdmitted() { return false; }

; CAP: FormatEE2EntryCapacity = 2
; CAP: FormatEE3EntryCapacity = 3
; CAP: HaydnSoloIssueClass
; CAP: cycleViolatesNamedSameCycleLaws

; PORT-DAG: Every explicit operand field
; PORT-DAG: MOVE32 rd,rs is 1R1W
; PORT-DAG: haydnHasAdmittedPerOpResourceRecords() { return false; }
; PORT-DAG: haydnMemoryObjectWaitCyclesAdmitted() { return false; }
; PORT-DAG: HAYDN_NAMED_SAME_CYCLE_LAWS_TAG
; PORT-DAG: arctan-sincos+csrw-set+abs-e0

; MODEL: IssueWidth = FormatEE3EntryCapacity
; MODEL: CompleteModel = 0
; MODEL-NOT: CompleteModel = 1

; SEQ: sequentialize after the product coissue probe rejected packing
; SEQ: recovery only; not an independent legality authority

; SFR: SFR 2R/1W
; SFR: haydnChargeDescNamedSfrPorts
; SFR: SFRReads
; SFR: SFRWrites

; R0: assert(isSoftZeroR0Clean
; R0: withDR64PackBase: soft-zero R0 must be clean before pack-base MatInt

; TXN: snapshotForCommitTxn
; TXN: restoreFromCommitTxn
; TXN: Sequentialize
; TXN: restoreTxn();

; IB: successorsAreScheduled
; IB: ScheduledMBBs.insert(BB);

; IBBRICK: haydn-postra-region-end-edges", cl::init(false)
; IBBRICK: haydn-postra-interblock", cl::init(false)
; IBBRICK: no PerSuccEdges invent
; IBBRICK: haydn-postra-waw-edges", cl::init(false)
; IBBRICK: IncludeStages(!EnableHaydnPostRAInterBlock
; IBBRICK: ReduceLatency(EnableHaydnPostRAInterBlock && IsBottomRegion &&
; IBBRICK-NOT: getPerSuccEdges
; IBBRICK-NOT: buildPerSuccEdges
; IBBRICK-NOT: class PerSuccEdges

; IBHDR: stay off until same-artifact evidence
; IBHDR: no PerSuccEdges invent
; IBHDR: Never a process-static lookup
