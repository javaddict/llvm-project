; RUN: %python %S/../../../../../utils/haydn/measure_sched_artifact.py self-test | FileCheck %s
; REQUIRES: haydn-registered-target
;
; Role: IR — one-artifact ordinary / StageCount1 / multi-stage measurement
; polarity. CompleteModel stays 0. Competitive II/density and NAT-IPC stay
; measure-miss. No Stage-0 IB/PP revive.
;
; CHECK: SELF-TEST OK
; CHECK-SAME: freestanding-12
; CHECK-SAME: per_op_admitted=false
; CHECK-SAME: competitive_claims=false
; CHECK-SAME: stagecount1_containment=1
; CHECK-SAME: complete_model=0
; CHECK-SAME: availability_aware=true
; CHECK-SAME: aggregate_only=true
; CHECK-SAME: ordinary_baseline_required=true
; CHECK-SAME: p8_object_required=true
; CHECK-SAME: resource_surface=bound
; CHECK-SAME: sequentialize_not_legality=true
; CHECK-SAME: golden_hash_binding_required=true
; CHECK-SAME: semantic_host_required_when_available=true
; CHECK-SAME: three_arm=ordinary,stagecount1,multistage
; CHECK-SAME: m18_unclaimable=true
; CHECK-SAME: nat_ipc_measured_miss=true
; CHECK-SAME: swps_asm_measured_miss=true
; CHECK-SAME: m_eval_only=M2,M17-M22
; CHECK-SAME: sf1_sf3_closed=false
; CHECK-SAME: object_mc_identity_only=true
; CHECK-SAME: ipc_proxy_measured_miss=true
; CHECK-SAME: t4_postra_unstuck=true
; CHECK-SAME: parcels_eq_ii_on_accept=true
; CHECK-SAME: hwloops_off_qualify=true
;
define void @measure_seat_anchor() {
  ret void
}