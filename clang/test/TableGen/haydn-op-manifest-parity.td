// RUN: clang-tblgen -I %p/../../include -I %p/../../../llvm/include %p/../../include/clang/Basic/BuiltinsHaydn.td -gen-haydn-op-manifest -o - | FileCheck %s
//
// CAPI-5 EXIT MET (2026-07-27) / G-CAPI-CLOSURE continuous suite:
// lock haydn_op_manifest.inc schema + sample contract rows
// (Imm / feature / Effect / PUB vs AE UNPUB) for every PublicEnabled op.
// No FormatID/slot/AltDesc. Residual product stress = BundleSim/ISS only.
// Field commas in prototypes are escaped to '_' by the emitter.

// Schema banner (column contract).
// CHECK: haydn_op_manifest.inc
// CHECK: HAYDN_OP_{PUB|UNPUB}
// CHECK: <effect>,<features>,<arity>,<imm_list>,<prototype>

// Pure scalar ImmArg sample: arctan(xy, uimm4 scale) — ImmCheck<1,0..15>.
// Columns: tag,td,pub,builtin,mnemonic,codegen,effect,features,arity,imm,proto
// CHECK-DAG: HAYDN_OP_PUB,arctan,arctan,__builtin_haydn_arctan,ARCTAN,,Pure,,2,1:0:15,int(int64_t_ int)

// Feature-gated State UA preload: pldwwua features=agu, Effect=State, ar_sel Imm.
// ImmCheck legal domain is 0..3 (2-bit ar_sel encoding; CB-149 restored
// ImmCheck0_3 — Sema surface; decode-side 2/3 policy stays with GE96-09).
// CHECK-DAG: HAYDN_OP_PUB,pldwwua,pldwwua,__builtin_haydn_pldwwua,PLDWWUA_POST,,State,agu,2,0:0:3,void(int_ void const*)

// Golden ordinary memory: Effect=Mem (WITH load).
// CHECK-DAG: HAYDN_OP_PUB,d_lw_with_imm,d_lw_with_imm,__builtin_haydn_d_lw_with_imm,{{[^,]+}},,Mem,

// ISA-65 fused round-sat-store (Effect=Mem; POST writeback + WITH, simm6).
// CHECK-DAG: HAYDN_OP_PUB,d_sw_f64rs_post_imm,d_sw_f64rs_post_imm,__builtin_haydn_d_sw_f64rs_post_imm,D_SW_F64RS_POST_IMM,,Mem,,3,2:-32:31,void*(int64_t_ void*_ int)
// CHECK-DAG: HAYDN_OP_PUB,d_sw_f64rs_with_imm,d_sw_f64rs_with_imm,__builtin_haydn_d_sw_f64rs_with_imm,D_SW_F64RS_WITH_IMM,,Mem,,3,2:-32:31,void(int64_t_ void*_ int)

// AE bag stays unpublished (C0.5 fail-closed default).
// CHECK-DAG: HAYDN_OP_UNPUB,{{[a-z0-9_]+}},{{[a-z0-9_]+}},__builtin_ae_{{[a-z0-9_]+}},

// Multi-Imm StateMem frexp pair: cbr_sel + simm8 stride.
// CHECK-DAG: HAYDN_OP_PUB,ldw_cb_imm_pair,ldw_cb_imm,__builtin_haydn_ldw_cb_imm_pair,D_LDW_CB_IMM,,StateMem,circular-buffer,4,2:0:1;3:-128:127,

// Summary counters (must stay consistent with fail-closed Effect enum).
// CHECK: Summary: PublicEnabled=
// CHECK-NEXT: Effect: Pure={{[0-9]+}} SFR={{[0-9]+}} State={{[0-9]+}} Mem={{[0-9]+}} StateMem={{[0-9]+}}
// CHECK-NEXT: Contract: with_features={{[0-9]+}} with_immchecks={{[0-9]+}}
// CHECK-NOT: Other=
// Data rows must not leak backend FormatID / AltDesc (banner may mention them).
// CHECK-NOT: {{^HAYDN_OP_.*FormatID}}
// CHECK-NOT: {{^HAYDN_OP_.*AltDesc}}
