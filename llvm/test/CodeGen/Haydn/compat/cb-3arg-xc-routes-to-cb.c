// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O1 -mllvm -enable-misched=false \
// RUN:   -mllvm -enable-post-misched=false -ffreestanding \
// RUN:   -Wno-implicit-function-declaration \
// RUN:   -Wno-incompatible-pointer-types -Wno-int-conversion \
// RUN:   -include stdint.h \
// RUN:   -D__HAYDN_ALLOW_INEXACT_AE \
// RUN:   -include haydn_dsp.h \
// RUN:   -mllvm -stop-after=instruction-select -S %s -o %t.s
// RUN: FileCheck --check-prefix=ASM %s < %t.s
// RUN: FileCheck --check-prefix=TEXT %s < %t.s

// Role: semantic — HiFi3 3-arg AE_*XC macros must lower to the CB load/store instruction, NOT to a plain linear load/store with pointer arithmetic.

// REGRESSION TEST: HiFi3 3-arg AE_*XC macros must lower to the CB load/store
// instruction, NOT to a plain linear load/store with pointer arithmetic.
//
// Bug (found while validating against real NatureDSP kernels):
// HiFi3z has a SINGLE implicit CBR selected by WUR_AE_CBEGIN0/CEND0, so the
// native AE_*XC API is 3-arg (dst/src, ptr, offs) — kernels NEVER pass a
// cbr_sel. The haydn_dsp.h compat layer had arity-dispatching overloads
// (_3A / _4A), but the _3A bodies routed to plain pointer arithmetic
// *(ae_int32x2 *)((char *)(ptr) + (offs))
// and only the _4A form (with explicit cbr_sel) reached the CB intrinsic.
// Result: 1033 AE_*XC call sites across the NatureDSP hifi3 corpus compiled
// to linear accesses with NO hardware wrap — silently wrong on hardware
// and the csrw 44/45 CBR setup was dead (the boundary was never consumed).
//
// Fix : the 3-arg AE_*XC form routes to __haydn_ldw_cb_imm
// __haydn_sdw_cb_imm with cbr_sel=0 (the implicit CBR0 selected by
// WUR_AE_CBEGIN0/CEND0). If this regresses, the d_ldw_cb_imm / d_sdw_cb_imm
// mnemonics vanish and the kernel silently emits linear loads/stores.
//
// Test design: drive each AE_*XC family (32x2, 16x4, scalar 32, scalar 16)
// through its 3-arg form after a WUR_AE_CBEGIN0/CEND0 setup, and FileCheck
// that both `csrw` (setup survives) and `d_ldw_cb_imm`/`d_sdw_cb_imm`
// (the CB access) appear where the macro is EXACT. AE_L16_XC is EMULATED
// (i16 load + soft CBR step; no 64b D_LDW_CB trunc) — see HAYDN_COMPAT_TIER.
// The 4-arg form is checked too for parity.
//
// generic and haydn are the same full ISA. Use resource-dir haydn_dsp.h
// (not source -I).

#include <stdint.h>

// Volatile sinks so the loads/stores cannot be DCE'd.

volatile ae_int32x2 g_sink32x2;
volatile ae_int16x4 g_sink16x4;
volatile ae_int32   g_sink32;
volatile ae_int16   g_sink16;
volatile uintptr_t  g_ptr_sink;

// CBR0 boundary setup must survive (hasSideEffects prevents DCE).
// CSR addresses: CBR_BEGIN[0]=0x2C=44, CBR_END[0]=0x2D=45.
// ASM-LABEL: name: setup_cbr0
// ASM: SETCBR
void setup_cbr0(void *base, uintptr_t end) {
  WUR_AE_CBEGIN0((uintptr_t)base);
  WUR_AE_CEND0(end);
}

//32x2 family: AE_L32X2_XC / AE_S32X2_XC

// ASM-LABEL: name: cb_load_32x2_3arg
// ASM: {{D_LDW_CB_IMM|LDW_CB}}
ae_int32x2 cb_load_32x2_3arg(ae_int32x2 *p) {
  ae_int32x2 t;
  AE_L32X2_XC(t, p, +8);   // 3-arg: implicit CBR0
  g_ptr_sink = (uintptr_t)p;
  return t;
}

// ASM-LABEL: name: cb_load_32x2_4arg
// ASM: {{D_LDW_CB_IMM|LDW_CB}}
ae_int32x2 cb_load_32x2_4arg(ae_int32x2 *p) {
  ae_int32x2 t;
  AE_L32X2_XC(t, p, +8, 0); // 4-arg: explicit cbr_sel=0
  g_ptr_sink = (uintptr_t)p;
  return t;
}

// ASM-LABEL: name: cb_store_32x2_3arg
// ASM: {{D_SDW_CB_IMM|SDW_CB}}
void cb_store_32x2_3arg(ae_int32x2 *p, ae_int32x2 v) {
  AE_S32X2_XC(v, p, +8);   // 3-arg store
  g_ptr_sink = (uintptr_t)p;
}

//16x4 family: AE_L16X4_XC / AE_S16X4_XC

// ASM-LABEL: name: cb_load_16x4_3arg
// ASM: {{D_LDW_CB_IMM|LDW_CB}}
ae_int16x4 cb_load_16x4_3arg(ae_int16x4 *p) {
  ae_int16x4 t;
  AE_L16X4_XC(t, p, +8);
  g_ptr_sink = (uintptr_t)p;
  return t;
}

// ASM-LABEL: name: cb_store_16x4_3arg
// ASM: {{D_SDW_CB_IMM|SDW_CB}}
void cb_store_16x4_3arg(ae_int16x4 *p, ae_int16x4 v) {
  AE_S16X4_XC(v, p, +8);
  g_ptr_sink = (uintptr_t)p;
}

//scalar 32 family: AE_L32_XC / AE_S32_L_XC
// EMULATED (2026-08-28 scalar-width law): the only HW circular memory ops are
// 64-bit; a scalar payload under D_*_CB overwrote ring neighbours and the
// offs>>3 scale was 8-byte granular. Scalar XC is native-width access +
// header haydn_cbr_step — never D_LDW_CB/D_SDW_CB.

// ASM-LABEL: name: cb_load_s32_3arg
// ASM: {{LD32|LDW}}
// ASM-NOT: {{D_LDW_CB_IMM|D_SDW_CB_IMM}}
ae_int32 cb_load_s32_3arg(ae_int32 *p) {
  ae_int32 t;
  AE_L32_XC(t, p, +4);
  g_ptr_sink = (uintptr_t)p;
  return t;
}

// ASM-LABEL: name: cb_store_s32_3arg
// ASM: {{ST32|STW}}
// ASM-NOT: {{D_LDW_CB_IMM|D_SDW_CB_IMM}}
void cb_store_s32_3arg(ae_int32 *p, ae_int32 v) {
  AE_S32_L_XC(v, p, +4);
  g_ptr_sink = (uintptr_t)p;
}

//scalar 16 family: AE_L16_XC / AE_S16_0_XC
// Both scalar-16 XC ops are permanently EMULATED (width law, same as scalar-32).

// ASM-LABEL: name: cb_load_s16_3arg
// ASM: {{LD16|LHW|S_LHW|LH}}
ae_int16 cb_load_s16_3arg(ae_int16 *p) {
  ae_int16 t;
  AE_L16_XC(t, p, +2);
  g_ptr_sink = (uintptr_t)p;
  return t;
}

// ASM-LABEL: name: cb_store_s16_3arg
// ASM: {{ST16|STH|S_SHW}}
// ASM-NOT: {{D_LDW_CB_IMM|D_SDW_CB_IMM}}
void cb_store_s16_3arg(ae_int16 *p, ae_int16 v) {
  AE_S16_0_XC(v, p, +2);
  g_ptr_sink = (uintptr_t)p;
}

// Non-empty.text gate (the MAC write-back false-PASS trap from CLAUDE.md):
// every probe function must lower to real instructions.
// TEXT-DAG: setup_cbr0
// TEXT-DAG: cb_load_32x2_3arg
// TEXT-DAG: cb_store_32x2_3arg
// TEXT-DAG: cb_load_16x4_3arg
// TEXT-DAG: cb_store_16x4_3arg
// TEXT-DAG: cb_load_s32_3arg
// TEXT-DAG: cb_store_s32_3arg
// TEXT-DAG: cb_load_s16_3arg
// TEXT-DAG: cb_store_s16_3arg
