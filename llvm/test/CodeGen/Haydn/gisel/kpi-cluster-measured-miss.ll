; RUN: %python %S/../../../../utils/haydn/measure_sched_artifact.py self-test | FileCheck %s
; REQUIRES: haydn-registered-target
;
; Role: IR — parked optional-KPI cluster stays measured-miss and clustered
; (not P0 densify). ipc_proxy is enc_fill/issue_width on the same artifact.
; No Stage-0 IB/PP revive. Native X2CMUL remains selectable; public AE
; wrappers stay fail-closed outside this seat.
;
; CHECK: SELF-TEST OK
; CHECK-SAME: nat_ipc_measured_miss=true
; CHECK-SAME: swps_asm_measured_miss=true
; CHECK-SAME: ipc_proxy_measured_miss=true
; CHECK-SAME: kpi_cluster=parked
; CHECK-SAME: p0_only=false
; CHECK-SAME: ipc_proxy_formula=enc_fill/issue_width
; CHECK-SAME: issue_width=3
;
define void @kpi_cluster_anchor() {
  ret void
}
