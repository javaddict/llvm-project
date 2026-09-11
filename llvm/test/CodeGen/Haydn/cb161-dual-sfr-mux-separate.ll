; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 -verify-machineinstrs -O2 -stop-after=postmisched < %s | FileCheck %s

; CB-161 (2026-08-21): dual SFR-write mux packing — SFR 1W/2R operand law.
;
; Bug: x2mux32 + x4mux16 in one BB lower to SLT → MOVESFR2GPR →
; MOVEGPR2SFR → MOVT chains whose SFR epochs share $sfr. The post-RA
; packer could stamp two SFR-writing members whose units collided
; (BACKEND FATAL "not unit-injective"; fixed separately by the GE96-11
; commit-site unit-twin rematch in HaydnHazardRecognizer
; commitPlacementForEmit), and countSFRPorts skipped member-shape
; anonymous implicit(-def) $sfr operands because generated member descs
; dropped the logical's Uses/Defs=[SFR] naming — leaving the golden SFR
; 2R/1W ceiling (VLIW_Engine_Compiler_Constraints.md §Registers: "Only one
; instruction per bundle is allowed to write to an SFR"; SFR read ports 2)
; unenforced at HR / commit / verify for committed members.
;
; Fix: countSFRPorts (HaydnPortModel.h) charges implicit $sfr operands on
; private Format E members via haydnIsPrivateFormatEMemberOpcode — operand
; truth, not desc naming. Dual SFR writers are also rejected by the shared
; intra-cycle WAW law; the port ceiling re-checks it independently.
;
; Test design: one BB holding both mux epochs (pred2 → x2mux32, pred4 →
; x4mux16), the exact repro shape from
; BundleSim benchmarks/haydn_intrin/repro_2026_08_21_sfr_dual_mux_unit_injective.c.
; The run is machine-verifier-clean: a dual-SFR-write bundle trips the
; shared WAW law and the port-budget re-check under -verify-machineinstrs
; at the late firewall, so silent co-issue fails this test loudly. The
; MIR pins below assert both epochs survive (both SLT-class writers and
; both MOVT consumers present, in SFR-legal order: writer before its
; reader). Exact bundling of unrelated memory/ALU ops is free to change;
; if a bundle body ever shows two `implicit-def $sfr` member lines, the
; SFR 1W law regressed — do NOT relax the CHECKs, re-read countSFRPorts /
; haydnCycleMembersExceedPortBudget. The per-bundle arithmetic seal is
; unittests/Target/Haydn/HaydnPortModelSFRMemberTest.cpp.

declare i32  @llvm.haydn.x2cmplt32(<2 x i32>, <2 x i32>)
declare i32  @llvm.haydn.x4cmplt16(<4 x i16>, <4 x i16>)
declare <2 x i32> @llvm.haydn.x2mux32(i32, <2 x i32>, <2 x i32>)
declare <4 x i16> @llvm.haydn.x4mux16(i32, <4 x i16>, <4 x i16>)

@g_x2sink = dso_local global <2 x i32> zeroinitializer, align 8
@g_x4sink = dso_local global <4 x i16> zeroinitializer, align 8

define dso_local i32 @cb161_dual_mux() {
; CHECK-LABEL: name: cb161_dual_mux
; All four SFR-epoch members exist (order-free: the scheduler may interleave
; the two epochs' memory/ALU filler, only the $sfr operand law is pinned).
; CHECK-DAG: X2SLT32_E{{[23]}}_E{{[012]}}_ALU{{[012]}}_R {{.*}}implicit-def $sfr
; CHECK-DAG: X2MOVT32_E{{[23]}}_E{{[012]}}_ALU{{[012]}}_R
; CHECK-DAG: X4SLT16_E{{[23]}}_E{{[012]}}_ALU{{[012]}}_R {{.*}}implicit-def $sfr
; CHECK-DAG: X4MOVT16_E{{[23]}}_E{{[012]}}_ALU{{[012]}}_R
entry:
  %p2 = call i32 @llvm.haydn.x2cmplt32(<2 x i32> <i32 1, i32 2>,
                                       <2 x i32> <i32 3, i32 4>)
  %p4 = call i32 @llvm.haydn.x4cmplt16(<4 x i16> <i16 1, i16 2, i16 3, i16 4>,
                                       <4 x i16> <i16 5, i16 6, i16 7, i16 8>)
  %m2 = call <2 x i32> @llvm.haydn.x2mux32(i32 %p2,
                                           <2 x i32> <i32 1, i32 2>,
                                           <2 x i32> <i32 3, i32 4>)
  %m4 = call <4 x i16> @llvm.haydn.x4mux16(i32 %p4,
                                           <4 x i16> <i16 1, i16 2, i16 3, i16 4>,
                                           <4 x i16> <i16 5, i16 6, i16 7, i16 8>)
  store <2 x i32> %m2, ptr @g_x2sink, align 8
  store <4 x i16> %m4, ptr @g_x4sink, align 8
  ret i32 0
}
