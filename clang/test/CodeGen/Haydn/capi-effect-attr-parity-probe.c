// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -O0 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=IR,IR-O0
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -O2 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=IR,IR-O2
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -O0 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=ATTR
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -ffreestanding -O2 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=ATTR
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding -O0 -c -o %t.o0.o %s
// RUN: %clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding -O2 -c -o %t.o2.o %s
// REQUIRES: haydn-registered-target
//
// C1.3 / G-PRED-SSA exit gate — per-record effect attribute parity:
//   Pure (x2add32 / add64 / x2cmplt32): memory(none)+speculatable; dead DCE at O2.
//   SFR  (x2slt32 / x2movt32 / movesfr2gpr): not memory(none); dead SFR writes live.
//   State (setcbr_begin / flar): not memory(none); void state writes survive.
//   Mem  (d_lw_with_imm): not memory(none).
//   StateMem (sdw_cb_imm / sdw_brev_imm): not memory(none); unused ret survives.
//
// Clang: only Effect=Pure gets Const. LLVM: haydn_* classes in IntrinsicsHaydn.td.
// Hexagon peer: circ/brev builtins are NONCONST (no Clang Const).
// IR checks: call survival / DCE. ATTR checks: declare attribute groups (DAG).

#include <haydn.h>

// ---------------------------------------------------------------------------
// Pure ALU
// ---------------------------------------------------------------------------

// IR-LABEL: @pure_x2add32_attrs
// IR: call <2 x i32> @llvm.haydn.x2add32
haydn_x2int32 pure_x2add32_attrs(haydn_x2int32 a, haydn_x2int32 b) {
  return haydn_x2add32(a, b);
}

// IR-LABEL: @pure_add64_attrs
// IR: call i64 @llvm.haydn.add64
long long pure_add64_attrs(long long a, long long b) {
  return haydn_add64(a, b);
}

// Dead pure result: O2 DCE (Const + IntrNoMem); O0 still emits the call.
// IR-LABEL: @pure_dead_x2add32_dce
// IR-O0: call <2 x i32> @llvm.haydn.x2add32
// IR-O2: entry:
// IR-O2-NEXT: ret void
void pure_dead_x2add32_dce(haydn_x2int32 a, haydn_x2int32 b) {
  (void)haydn_x2add32(a, b);
}

// C1.1 SSA predicates stay Pure (contrast ambient SFR below).
// IR-LABEL: @pure_x2cmplt_attrs
// IR: call i32 @llvm.haydn.x2cmplt32
haydn_pred2_t pure_x2cmplt_attrs(haydn_x2int32 a, haydn_x2int32 b) {
  return haydn_x2cmplt32(a, b);
}

// ---------------------------------------------------------------------------
// SFR ambient — F19 / C1.3: no Const, IntrHasSideEffects
// ---------------------------------------------------------------------------

// IR-LABEL: @sfr_x2slt32_attrs
// IR: call void @llvm.haydn.x2slt32
void sfr_x2slt32_attrs(haydn_x2int32 a, haydn_x2int32 b) {
  (void)haydn_x2slt32(a, b);
}

// IR-LABEL: @sfr_x2movt32_attrs
// IR: call <2 x i32> @llvm.haydn.x2movt32
haydn_x2int32 sfr_x2movt32_attrs(haydn_x2int32 dst, haydn_x2int32 src) {
  return haydn_x2movt32(dst, src);
}

// IR-LABEL: @sfr_movesfr2gpr_attrs
// IR: call i32 @llvm.haydn.movesfr2gpr
unsigned sfr_movesfr2gpr_attrs(void) {
  return haydn_movesfr2gpr();
}

// Two-epoch reverse order: both ambient movt survive CSE at O2 (C1.2+C1.3).
// IR-LABEL: @sfr_two_epoch_movt_no_cse
// IR: call void @llvm.haydn.x2slt32
// IR: call <2 x i32> @llvm.haydn.x2movt32
// IR: call void @llvm.haydn.x2slt32
// IR: call <2 x i32> @llvm.haydn.x2movt32
haydn_x2int32 sfr_two_epoch_movt_no_cse(haydn_x2int32 a, haydn_x2int32 b,
                                        haydn_x2int32 c, haydn_x2int32 d,
                                        haydn_x2int32 t0, haydn_x2int32 t1) {
  (void)haydn_x2slt32(a, b);
  haydn_x2int32 r0 = haydn_x2movt32(t0, t1);
  (void)haydn_x2slt32(c, d);
  haydn_x2int32 r1 = haydn_x2movt32(t0, t1);
  return haydn_x2add32(r0, r1);
}

// ---------------------------------------------------------------------------
// State — CSR/CBR/AR
// ---------------------------------------------------------------------------

// IR-LABEL: @state_setcbr_survives
// IR: call void @llvm.haydn.setcbr.begin
void state_setcbr_survives(int begin) {
  haydn_setcbr_begin(0, begin);
}

// IR-LABEL: @state_flar_survives
// IR: call void @llvm.haydn.flar
void state_flar_survives(void) {
  haydn_flar(0);
}

// ---------------------------------------------------------------------------
// Mem — Golden LS
// ---------------------------------------------------------------------------

// IR-LABEL: @mem_d_lw_with_imm_attrs
// IR: call i64 @llvm.haydn.d.lw.with.imm
long long mem_d_lw_with_imm_attrs(const void *base) {
  return haydn_d_lw_with_imm(base, 0);
}

// ---------------------------------------------------------------------------
// StateMem — CB / BREV
// ---------------------------------------------------------------------------

// IR-LABEL: @statemem_sdw_cb_survives
// IR: call ptr @llvm.haydn.sdw.cb.imm
void statemem_sdw_cb_survives(long long data, void *base) {
  (void)haydn_sdw_cb_imm(data, base, 0, 1);
}

// IR-LABEL: @statemem_sdw_brev_survives
// IR: call ptr @llvm.haydn.sdw.brev.imm
void statemem_sdw_brev_survives(long long data, void *base) {
  (void)haydn_sdw_brev_imm(data, base, 1);
}

// ---------------------------------------------------------------------------
// ATTR pass: declare attribute groups (separate FileCheck; full-file DAG)
// Pure ends with memory(none); non-Pure do not.
// ---------------------------------------------------------------------------

// Pure ALU / C1.1 SSA: memory(none)+speculatable (shared attr group OK).
// ATTR-DAG: declare {{.*}} @llvm.haydn.x2add32{{.*}} #[[PURE:[0-9]+]]
// ATTR-DAG: declare {{.*}} @llvm.haydn.add64{{.*}} #[[PURE]]
// ATTR-DAG: declare {{.*}} @llvm.haydn.x2cmplt32{{.*}} #[[PURE]]
// ATTR-DAG: attributes #[[PURE]] = { {{.*}}speculatable{{.*}}memory(none) }

// SFR ambient: same willreturn group; not memory(none).
// ATTR-DAG: declare {{.*}} @llvm.haydn.x2slt32{{.*}} #[[SFR:[0-9]+]]
// ATTR-DAG: declare {{.*}} @llvm.haydn.x2movt32{{.*}} #[[SFR]]
// ATTR-DAG: declare {{.*}} @llvm.haydn.movesfr2gpr{{.*}} #[[SFR]]
// ATTR-DAG: attributes #[[SFR]] = { {{.*}}willreturn }

// State: setcbr is memory(write); flar is willreturn (may share SFR group).
// ATTR-DAG: declare {{.*}} @llvm.haydn.setcbr.begin{{.*}} #[[STATE:[0-9]+]]
// ATTR-DAG: declare {{.*}} @llvm.haydn.flar{{.*}} #[[FLAR:[0-9]+]]
// ATTR-DAG: attributes #[[STATE]] = { {{.*}}memory(write) }

// Mem / StateMem — IntrArgMemOnly → memory(argmem: read|write) (C2.3 AA).
// CB/BREV stores may share one attribute group.
// ATTR-DAG: declare {{.*}} @llvm.haydn.d.lw.with.imm{{.*}} #[[MEM:[0-9]+]]
// ATTR-DAG: declare {{.*}} @llvm.haydn.sdw.cb.imm{{.*}} #[[CB:[0-9]+]]
// ATTR-DAG: declare {{.*}} @llvm.haydn.sdw.brev.imm{{.*}} #[[CB]]
// ATTR-DAG: attributes #[[MEM]] = { {{.*}}memory(argmem: read) }
// ATTR-DAG: attributes #[[CB]] = { {{.*}}memory(argmem: write) }
