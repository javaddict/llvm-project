// REQUIRES: haydn-registered-target
// RUN: clang -target haydn-unknown-elf -mcpu=haydn \
// RUN:   -mllvm -global-isel-abort=1 -O2 -ffreestanding \
// RUN:   -emit-llvm -S -o - %s | FileCheck %s --check-prefix=IR
// RUN: not clang -target haydn-unknown-elf -mcpu=haydn -ffreestanding \
// RUN:   -fsyntax-only -DTEST_ADD64X2_STRICT %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=STRICT
//
// Residual pure / memory / MAC / state family value/object matrix.
// Full public EXACT+EMULATED inventory carries TD-authored OracleId
// registrations (floor 671). Independent host value + non-empty object
// evidence extends beyond family representatives. Permanent empty-body
// dual-64 ADD64X2_* stay fail-closed (no silent body under default law).
// Empty object output fails.

#include <haydn_dsp.h>

/* Full public surface inventory floor (TAG_COUNT minus permanent UNSUPPORTED). */
_Static_assert(HAYDN_AE_COMPAT_TAG_COUNT >= 600, "full public AE tier inventory");
_Static_assert(HAYDN_AE_ORACLE_COUNT >= 671, "full public AE oracle inventory floor");
_Static_assert(HAYDN_AE_ORACLE_COUNT == HAYDN_AE_COMPAT_TAG_COUNT - 2,
               "oracle count is TAG_COUNT minus permanent UNSUPPORTED pair");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_ == HAYDN_COMPAT_UNSUPPORTED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64X2_vector == HAYDN_COMPAT_UNSUPPORTED, "");
_Static_assert(__HAYDN_AE_COMPAT_STRICT == 1, "strict default");
/* AE_MULAAAAQ16 is unconditional Path-A haydn_fmulaa16_hs_11_00.
 * vec_dot16x16_fast dest-typed Path-B stays AE_MULAF16X4SS (1190).
 * Two-lane Path-A host is 380 (haydn-dsp-mulaaaaq16-value.c). */
#ifndef AE_MULAAAAQ16
_Static_assert(0, "AE_MULAAAAQ16 must be defined unconditionally");
#endif

/* ---- Pure residual family (TD-authored emu.* OracleIds) ---- */
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SUB32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD32S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ABS32S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_NEG32S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD16 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD16S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ABS16S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_OR32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_AND32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_XOR32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ZERO32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ZERO64 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MOV == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_AND64 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ADD32, "emu.add32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SUB32, "emu.sub32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ADD32S, "emu.add32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ABS32S, "emu.abs32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_NEG32S, "emu.neg32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ADD16, "emu.add16") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ADD16S, "emu.add16s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ABS16S, "emu.abs16s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_OR32, "emu.or32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_AND32, "emu.and32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_XOR32, "emu.xor32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ZERO32, "emu.zero32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ZERO64, "emu.zero64") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MOV, "emu.mov") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ADD64, "emu.add64") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_AND64, "emu.and64") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAI32, "emu.slai32") == 0, "");
/* Expanded pure residual beyond family representatives (full authored). */
_Static_assert(HAYDN_COMPAT_TIER_AE_SUB32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SUB32S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SUB16 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SUB16S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ABS64 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ABS64S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ADD64S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_ZERO16 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_NOT32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLLI32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAI32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRLI32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_AND16 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_OR16 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_NEG16S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MAX32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MIN32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MAX16 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MIN16 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MAXABS32S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SUB32, "emu.sub32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SUB32S, "emu.sub32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SUB16, "emu.sub16") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SUB16S, "emu.sub16s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ABS64, "emu.abs64") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ABS64S, "emu.abs64s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ADD64S, "emu.add64s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ZERO16, "emu.zero16") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_NOT32, "emu.not32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLLI32, "emu.slli32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SRAI32, "emu.srai32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SRLI32, "emu.srli32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_AND16, "emu.and16") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_OR16, "emu.or16") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_NEG16S, "emu.neg16s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MAX32, "emu.max32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MIN32, "emu.min32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MAX16, "emu.max16") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MIN16, "emu.min16") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MAXABS32S, "emu.maxabs32s") == 0, "");

/* ---- Memory residual family (TD-authored + MemEffect) ---- */
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2_I == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_S32X2_I == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_L16X4_I == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_L32X2_I == 1, "L32X2_I mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_S32X2_I == 1, "S32X2_I mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_L16X4_I == 1, "L16X4_I mem");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_L32X2_I, "emu.l32x2_i") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_S32X2_I, "emu.s32x2_i") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_L16X4_I, "emu.l16x4_i") == 0, "");
/* Expanded memory residual beyond family representatives. */
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2_IP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_S32X2_IP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2_XP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_L16X4_IP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_S16X4_IP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32_IP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_S32_L_IP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32X2_X == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_S32X2_X == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_L32X2_IP == 1, "L32X2_IP mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_S32X2_IP == 1, "S32X2_IP mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_L32X2_XP == 1, "L32X2_XP mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_L16X4_IP == 1, "L16X4_IP mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_S16X4_IP == 1, "S16X4_IP mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_L32_IP == 1, "L32_IP mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_S32_L_IP == 1, "S32_L_IP mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_L32X2_X == 1, "L32X2_X mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_S32X2_X == 1, "S32X2_X mem");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_L32X2_IP, "emu.l32x2_ip") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_S32X2_IP, "emu.s32x2_ip") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_L32X2_XP, "emu.l32x2_xp") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_L16X4_IP, "emu.l16x4_ip") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_S16X4_IP, "emu.s16x4_ip") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_L32_IP, "emu.l32_ip") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_S32_L_IP, "emu.s32_l_ip") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_L32X2_X, "emu.l32x2_x") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_S32X2_X, "emu.s32x2_x") == 0, "");
/* Peer memory residual (indexed / post-inc variants beyond reps). */
_Static_assert(HAYDN_COMPAT_TIER_AE_S32X2_XP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_L16X4_X == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_L32_XP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_L16_IP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_S16X4_X == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_L16X4_XP == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_S32X2_XP == 1, "S32X2_XP mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_L16X4_X == 1, "L16X4_X mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_L32_XP == 1, "L32_XP mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_L16_IP == 1, "L16_IP mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_S16X4_X == 1, "S16X4_X mem");
_Static_assert(HAYDN_AE_MEMEFFECT_AE_L16X4_XP == 1, "L16X4_XP mem");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_S32X2_XP, "emu.s32x2_xp") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_L16X4_X, "emu.l16x4_x") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_L32_XP, "emu.l32_xp") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_L16_IP, "emu.l16_ip") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_S16X4_X, "emu.s16x4_x") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_L16X4_XP, "emu.l16x4_xp") == 0, "");

/* ---- MAC residual family (TD-authored) ---- */
_Static_assert(HAYDN_COMPAT_TIER_AE_MULP32X2 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MULFP32X2RAS == HAYDN_COMPAT_EMULATED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MULP32X2, "emu.mulp32x2") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MULFP32X2RAS, "emu.mulfp32x2ras") == 0, "");
/* Expanded MAC residual beyond family representatives. */
_Static_assert(HAYDN_COMPAT_TIER_AE_MUL16JS == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MUL64_SS_LL == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MULAFP32X2RAS == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MULS32X2 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MUL16JS, "emu.mul16js") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MUL64_SS_LL, "emu.mul64_ss_ll") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MULAFP32X2RAS, "emu.mulafp32x2ras") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MULS32X2, "emu.muls32x2") == 0, "");
/* Peer MAC residual (lane-product / accumulate variants). */
_Static_assert(HAYDN_COMPAT_TIER_AE_MUL64_SS_HH == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MULA64_SS_LL == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MUL32X16_L0 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MULA32X16_L0 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MUL64_SS_HH, "emu.mul64_ss_hh") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MULA64_SS_LL, "emu.mula64_ss_ll") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MUL32X16_L0, "emu.mul32x16_l0") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MULA32X16_L0, "emu.mula32x16_l0") == 0, "");
/* Dest-typed vec_dot16 path: AE_MULAF16X4SS is X4MULA16S + union bitcast. */
_Static_assert(HAYDN_COMPAT_TIER_AE_MULAF16X4SS == HAYDN_COMPAT_EMULATED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MULAF16X4SS, "emu.mulaf16x4ss") == 0, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MULAAAAQ16 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MULAAAAQ16, "emu.mulaaaaq16") == 0, "");

/* ---- State residual family (TD-authored SoftState + OracleId) ---- */
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS32 == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAS32 == HAYDN_COMPAT_EXACT, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAS32S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SAR == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_AE_SOFTSTATE_AE_SLAS32 == 1, "SLAS32 state");
_Static_assert(HAYDN_AE_SOFTSTATE_AE_SRAS32 == 1, "SRAS32 state");
_Static_assert(HAYDN_AE_SOFTSTATE_AE_SLAS32S == 1, "SLAS32S state");
_Static_assert(HAYDN_AE_SOFTSTATE_AE_SAR == 1, "SAR state");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAS32, "exact.slas32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SRAS32, "exact.sras32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAS32S, "softsat.slaa32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SAR, "state.sar") == 0, "");

/* Curated prefixes must not be residual emu.* overwrite. */
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SEL32_LH, "exact.sel32_lh") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SEL32_HH, "exact.sel32_hh") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SEL32_LL, "exact.sel32_ll") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SEL32_HL, "exact.sel32_hl") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_CVTQ56A32S, "ae0.cvtq56a32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_CVT16X4, "ae0.cvt16x4") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAA32S, "softsat.slaa32s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAA64S, "ae0.slaa64s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MAXABS16S, "emu.maxabs16s") == 0, "");
/* Authored pure residual peers: arithmetic shift / lane shift. */
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAA32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAA16 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRAI64 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SRAA32, "emu.sraa32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SRAA16, "emu.sraa16") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SRAI64, "emu.srai64") == 0, "");

//===----------------------------------------------------------------------===//
// Pure family — bag zero / logical host value oracles
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_zero64
// IR: ret i64 0
// ASM-LABEL: resid_zero64:
// OBJ-LABEL: <resid_zero64>:
// OBJ: addi32
ae_int64 resid_zero64(void) { return AE_ZERO64(); }

// IR-LABEL: @resid_zero32_bag
// IR: ret i64 0
// ASM-LABEL: resid_zero32_bag:
// OBJ-LABEL: <resid_zero32_bag>:
// OBJ: addi32
ae_int64 resid_zero32_bag(void) { return __AE_TO_I64(AE_ZERO32()); }

// IR-LABEL: @resid_and32_mask
// IR: ret i64 {{4294967297|0x100000001}}
// ASM-LABEL: resid_and32_mask:
// OBJ-LABEL: <resid_and32_mask>:
// OBJ: addi32
ae_int64 resid_and32_mask(void) {
  ae_int32x2 a = {3, 1};
  ae_int32x2 b = {1, 3};
  return __AE_TO_I64(AE_AND32(a, b));
}

// IR-LABEL: @resid_or32_bits
// IR: ret i64 {{12884901891|0x300000003}}
// ASM-LABEL: resid_or32_bits:
// OBJ-LABEL: <resid_or32_bits>:
// OBJ: addi32
ae_int64 resid_or32_bits(void) {
  ae_int32x2 a = {1, 2};
  ae_int32x2 b = {2, 1};
  return __AE_TO_I64(AE_OR32(a, b));
}

// IR-LABEL: @resid_xor32_bits
// IR: ret i64 {{4294967297|0x100000001}}
// ASM-LABEL: resid_xor32_bits:
// OBJ-LABEL: <resid_xor32_bits>:
// OBJ: addi32
ae_int64 resid_xor32_bits(void) {
  ae_int32x2 a = {3, 1};
  ae_int32x2 b = {2, 0};
  return __AE_TO_I64(AE_XOR32(a, b));
}

// IR-LABEL: @resid_add64_one
// IR: ret i64 3
// ASM-LABEL: resid_add64_one:
// OBJ-LABEL: <resid_add64_one>:
// OBJ: addi32
ae_int64 resid_add64_one(void) {
  return AE_ADD64((ae_int64)1, (ae_int64)2);
}

// IR-LABEL: @resid_and64_mask
// IR: ret i64 1
// ASM-LABEL: resid_and64_mask:
// OBJ-LABEL: <resid_and64_mask>:
// OBJ: addi32
ae_int64 resid_and64_mask(void) {
  return AE_AND64((ae_int64)0x3, (ae_int64)0x1);
}

// IR-LABEL: @resid_slai32_one
// IR: call {{.*}} @llvm.haydn.x2sll32
// IR: call {{.*}} @llvm.haydn.movad32.low
int resid_slai32_one(void) {
  return (int)__AE_S32_LO(AE_SLAI32(((ae_int32x2){1, 0}), 1));
}

// Independent host values beyond family reps (C shift, not native SIMD).
// IR-LABEL: @resid_slai32_two
// IR: call {{.*}} @llvm.haydn.x2sll32
// IR: call {{.*}} @llvm.haydn.movad32.low
int resid_slai32_two(void) {
  return (int)__AE_S32_LO(AE_SLAI32(((ae_int32x2){1, 0}), 2));
}

//===----------------------------------------------------------------------===//
// Pure family — dual-sat / lane arith non-empty object path
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_add32s
// IR: call {{.*}}@llvm.haydn.x2add32s
// ASM-LABEL: resid_add32s:
// ASM: {{x2add32s|add32s}}
// OBJ-LABEL: <resid_add32s>:
// OBJ: {{x2add32s|add32s}}
ae_int32x2 resid_add32s(ae_int32x2 a, ae_int32x2 b) { return AE_ADD32S(a, b); }

// Constant inputs into native dual-sat add; host may not constant-fold.
// IR-LABEL: @resid_add32s_host
// IR: call {{.*}}@llvm.haydn.x2add32s
// IR: ret i64
// ASM-LABEL: resid_add32s_host:
// ASM: {{x2add32s|add32s}}
// OBJ-LABEL: <resid_add32s_host>:
// OBJ: {{x2add32s|add32s}}
ae_int64 resid_add32s_host(void) {
  ae_int32x2 a = {1, 1};
  ae_int32x2 b = {2, 2};
  return __AE_TO_I64(AE_ADD32S(a, b));
}

// IR-LABEL: @resid_abs32s
// IR: call {{.*}}@llvm.haydn.x2abs32s
// ASM-LABEL: resid_abs32s:
// ASM: {{x2abs32s|abs32s}}
// OBJ-LABEL: <resid_abs32s>:
// OBJ: {{x2abs32s|abs32s}}
ae_int32x2 resid_abs32s(ae_int32x2 a) { return AE_ABS32S(a); }

// Constant inputs into native dual abs; host may not constant-fold.
// IR-LABEL: @resid_abs32s_host
// IR: call {{.*}}@llvm.haydn.x2abs32s
// IR: ret i64
// ASM-LABEL: resid_abs32s_host:
// ASM: {{x2abs32s|abs32s}}
// OBJ-LABEL: <resid_abs32s_host>:
// OBJ: {{x2abs32s|abs32s}}
ae_int64 resid_abs32s_host(void) {
  ae_int32x2 a = {-1, -2};
  return __AE_TO_I64(AE_ABS32S(a));
}

// IR-LABEL: @resid_neg32s
// IR: call {{.*}}@llvm.haydn.x2neg32s
// ASM-LABEL: resid_neg32s:
// ASM: {{x2neg32s|neg32s}}
// OBJ-LABEL: <resid_neg32s>:
// OBJ: {{x2neg32s|neg32s}}
ae_int32x2 resid_neg32s(ae_int32x2 a) { return AE_NEG32S(a); }

// IR-LABEL: @resid_add16
// IR: call {{.*}}@llvm.haydn.x4add16
// ASM-LABEL: resid_add16:
// ASM: x4add16
// OBJ-LABEL: <resid_add16>:
// OBJ: x4add16
ae_int64 resid_add16(void) {
  ae_int16x4 a = {1, 1, 1, 1};
  ae_int16x4 b = {1, 1, 1, 1};
  return __AE_TO_I64(AE_ADD16(a, b));
}

// IR-LABEL: @resid_add16s
// IR: call {{.*}}@llvm.haydn.x4add16s
// ASM-LABEL: resid_add16s:
// ASM: {{x4add16s|add16s}}
// OBJ-LABEL: <resid_add16s>:
// OBJ: {{x4add16s|add16s}}
ae_int16x4 resid_add16s(ae_int16x4 a, ae_int16x4 b) { return AE_ADD16S(a, b); }

// IR-LABEL: @resid_abs16s
// IR: call {{.*}}@llvm.haydn.x4abs16s
// ASM-LABEL: resid_abs16s:
// ASM: {{abs|ABS|x4abs|X4ABS}}
// OBJ-LABEL: <resid_abs16s>:
// OBJ: {{abs|ABS|x4abs|X4ABS}}
ae_int64 resid_abs16s(void) {
  ae_int16x4 a = {-1, -1, -1, -1};
  return __AE_TO_I64(AE_ABS16S(a));
}

//===----------------------------------------------------------------------===//
// Memory residual — non-empty load/store object path
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_l32x2_i
// IR: load {{.*}}<2 x i32>
// ASM-LABEL: resid_l32x2_i:
// ASM: {{ld64|ldw|LDW|LD64}}
// OBJ-LABEL: <resid_l32x2_i>:
// OBJ: {{ld64|ldw|LDW|LD64}}
ae_int32x2 resid_l32x2_i(const ae_int32x2 *p) { return AE_L32X2_I(p, 0); }

// IR-LABEL: @resid_s32x2_i
// IR: store {{.*}}<2 x i32>
// ASM-LABEL: resid_s32x2_i:
// ASM: {{st64|sdw|SDW|ST64}}
// OBJ-LABEL: <resid_s32x2_i>:
// OBJ: {{st64|sdw|SDW|ST64}}
void resid_s32x2_i(ae_int32x2 v, ae_int32x2 *p) { AE_S32X2_I(v, p, 0); }

// IR-LABEL: @resid_l16x4_i
// IR: load {{.*}}<4 x i16>
// ASM-LABEL: resid_l16x4_i:
// ASM: {{ld64|ldw|LDW|LD64}}
// OBJ-LABEL: <resid_l16x4_i>:
// OBJ: {{ld64|ldw|LDW|LD64}}
ae_int16x4 resid_l16x4_i(const ae_int16x4 *p) { return AE_L16X4_I(p, 0); }

//===----------------------------------------------------------------------===//
// MAC residual — native multiply non-empty object
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_mulp32x2
// IR: call {{.*}}@llvm.haydn.x2mul32
// ASM-LABEL: resid_mulp32x2:
// ASM: x2mul32
// OBJ-LABEL: <resid_mulp32x2>:
// OBJ: x2mul32
ae_int64 resid_mulp32x2(void) {
  ae_int32x2 a = {2, 3};
  ae_int32x2 b = {4, 5};
  return __AE_TO_I64(AE_MULP32X2(a, b));
}

// IR-LABEL: @resid_mulfp32x2ras
// IR: call {{.*}}@llvm.haydn.{{x2fmul32rs|x2fmul}}
// ASM-LABEL: resid_mulfp32x2ras:
// ASM: {{x2fmul32rs|fmul32rs|FMUL}}
// OBJ-LABEL: <resid_mulfp32x2ras>:
// OBJ: {{x2fmul32rs|fmul32rs|FMUL}}
ae_int64 resid_mulfp32x2ras(void) {
  ae_int32x2 a = {0x40000000, 0x40000000};
  ae_int32x2 b = {0x40000000, 0x40000000};
  return __AE_TO_I64(AE_MULFP32X2RAS(a, b));
}

// Dest-typed AE_MULAF16X4SS onto ae_f32x2 accs (vec_dot16 documented path).
// O2 keeps both X4MULA16S dests as i64 extractvalues (union assign +
// __AE_TO_I64 cancel). A splat writeback would drop the high dest.
// IR-LABEL: @resid_mulaf16x4ss_dest
// IR: call { i64, i64 } @llvm.haydn.x4mula16s
// IR-DAG: extractvalue { i64, i64 } {{.*}}, 0
// IR-DAG: extractvalue { i64, i64 } {{.*}}, 1
int64_t resid_mulaf16x4ss_dest(void) {
  ae_f32x2 vaf = AE_MOVI(0);
  ae_f32x2 vbf = AE_MOVI(0);
  ae_int16x4 x = AE_MOVDA16(1);
  ae_int16x4 y = AE_MOVDA16(1);
  AE_MULAF16X4SS(vaf, vbf, x, y);
  return __AE_TO_I64(vaf) ^ __AE_TO_I64(vbf);
}

// Unconditional Path-A AE_MULAAAAQ16: two-lane fmulaa16.hs.11.00, not X4MULA16S.
// IR-LABEL: @resid_mulaaaaq16_path_a
// IR: call {{.*}}@llvm.haydn.fmulaa16.hs.11.00
// IR-NOT: x4mula16s
ae_int64 resid_mulaaaaq16_path_a(ae_int16x4 a, ae_int16x4 b) {
  ae_int64 acc = AE_ZERO64();
  AE_MULAAAAQ16(acc, a, b);
  return acc;
}

//===----------------------------------------------------------------------===//
// State residual — ambient SAR SoftState host value / non-empty object
//===----------------------------------------------------------------------===//

// WUR_AE_SAR(1) folds into x2sll32 shift imm=1 on both lanes.
// IR-LABEL: @resid_slas32_sar
// IR: call {{.*}}@llvm.haydn.x2sll32({{.*}}splat (i32 1), i32 1)
// ASM-LABEL: resid_slas32_sar:
// ASM: {{x2sll32|sll32|SLL}}
// OBJ-LABEL: <resid_slas32_sar>:
// OBJ: {{x2sll32|sll32|SLL}}
ae_int64 resid_slas32_sar(void) {
  WUR_AE_SAR(1);
  ae_int32x2 a = {1, 1};
  return __AE_TO_I64(AE_SLAS32(a));
}

// WUR_AE_SAR(1) folds into x2sra32 shift imm=1 on both lanes.
// IR-LABEL: @resid_sras32_sar
// IR: call {{.*}}@llvm.haydn.x2sra32({{.*}}splat (i32 2), i32 1)
// ASM-LABEL: resid_sras32_sar:
// ASM: {{x2sra32|sra32|SRA}}
// OBJ-LABEL: <resid_sras32_sar>:
// OBJ: {{x2sra32|sra32|SRA}}
ae_int64 resid_sras32_sar(void) {
  WUR_AE_SAR(1);
  ae_int32x2 a = {2, 2};
  return __AE_TO_I64(AE_SRAS32(a));
}

// Host oracle: soft-sat ambient SAR left (state + softsat residual).
// IR-LABEL: @resid_slas32s_sar
// IR: ret i64 8589934594
// ASM-LABEL: resid_slas32s_sar:
// OBJ-LABEL: <resid_slas32s_sar>:
// OBJ: addi32
ae_int64 resid_slas32s_sar(void) {
  WUR_AE_SAR(1);
  ae_int32x2 a = {1, 1};
  return __AE_TO_I64(AE_SLAS32S(a));
}

// SoftState SAR register read-back after WUR.
// IR-LABEL: @resid_sar_roundtrip
// IR: ret i32 3
// ASM-LABEL: resid_sar_roundtrip:
// OBJ-LABEL: <resid_sar_roundtrip>:
// OBJ: addi32
int resid_sar_roundtrip(void) {
  WUR_AE_SAR(3);
  return (int)AE_SAR;
}

//===----------------------------------------------------------------------===//
// Expanded pure residual — independent value/object beyond family reps
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_zero16_bag
// IR: ret i64 0
// ASM-LABEL: resid_zero16_bag:
// OBJ-LABEL: <resid_zero16_bag>:
// OBJ: addi32
ae_int64 resid_zero16_bag(void) { return __AE_TO_I64(AE_ZERO16()); }

// IR-LABEL: @resid_not32_bits
// IR: ret i32 -2
// ASM-LABEL: resid_not32_bits:
// OBJ-LABEL: <resid_not32_bits>:
// OBJ: addi32
int resid_not32_bits(void) { return (int)AE_NOT32(1); }

// IR-LABEL: @resid_and16_mask
// IR: ret i64 {{281479271743489|0x1000100010001}}
// ASM-LABEL: resid_and16_mask:
// OBJ-LABEL: <resid_and16_mask>:
// OBJ: addi32
ae_int64 resid_and16_mask(void) {
  ae_int16x4 a = {3, 1, 3, 1};
  ae_int16x4 b = {1, 3, 1, 3};
  return __AE_TO_I64(AE_AND16(a, b));
}

// IR-LABEL: @resid_or16_bits
// IR: ret i64 {{844437815230467|0x3000300030003}}
// ASM-LABEL: resid_or16_bits:
// OBJ-LABEL: <resid_or16_bits>:
// OBJ: addi32
ae_int64 resid_or16_bits(void) {
  ae_int16x4 a = {1, 2, 1, 2};
  ae_int16x4 b = {2, 1, 2, 1};
  return __AE_TO_I64(AE_OR16(a, b));
}

// IR-LABEL: @resid_sub32
// IR: call {{.*}}@llvm.haydn.x2sub32
// ASM-LABEL: resid_sub32:
// ASM: {{x2sub32|sub32}}
// OBJ-LABEL: <resid_sub32>:
// OBJ: {{x2sub32|sub32}}
ae_int32x2 resid_sub32(ae_int32x2 a, ae_int32x2 b) { return AE_SUB32(a, b); }

// IR-LABEL: @resid_slli32_one
// IR: call {{.*}}@llvm.haydn.x2sll32
// ASM-LABEL: resid_slli32_one:
// ASM: {{x2sll32|sll32|SLL}}
// OBJ-LABEL: <resid_slli32_one>:
// OBJ: {{x2sll32|sll32|SLL}}
ae_int32x2 resid_slli32_one(ae_int32x2 a) { return AE_SLLI32(a, 1); }

// IR-LABEL: @resid_abs64_neg
// IR: call {{.*}}@llvm.haydn.abs64
// ASM-LABEL: resid_abs64_neg:
// ASM: {{abs64|ABS64}}
// OBJ-LABEL: <resid_abs64_neg>:
// OBJ: {{abs64|ABS64}}
ae_int64 resid_abs64_neg(ae_int64 a) { return AE_ABS64(a); }

// IR-LABEL: @resid_neg16s
// IR: call {{.*}}@llvm.haydn.
// ASM-LABEL: resid_neg16s:
// ASM: {{x4sub16s|sub16s|neg|NEG|x4neg|SUB}}
// OBJ-LABEL: <resid_neg16s>:
// OBJ: {{x4sub16s|sub16s|neg|NEG|x4neg|SUB}}
ae_int16x4 resid_neg16s(ae_int16x4 a) { return AE_NEG16S(a); }

//===----------------------------------------------------------------------===//
// Expanded memory residual — post-inc load/store non-empty object
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_l32x2_ip
// IR: load
// ASM-LABEL: resid_l32x2_ip:
// ASM: {{ld64|ldw|LDW|LD64|addi}}
// OBJ-LABEL: <resid_l32x2_ip>:
// OBJ: {{ld64|ldw|LDW|LD64|addi}}
ae_int32x2 resid_l32x2_ip(ae_int32x2 *p) {
  ae_int32x2 v;
  AE_L32X2_IP(v, p, 8);
  return v;
}

// IR-LABEL: @resid_s32x2_ip
// IR: store
// ASM-LABEL: resid_s32x2_ip:
// ASM: {{st64|sdw|SDW|ST64|addi}}
// OBJ-LABEL: <resid_s32x2_ip>:
// OBJ: {{st64|sdw|SDW|ST64|addi}}
void resid_s32x2_ip(ae_int32x2 v, ae_int32x2 *p) { AE_S32X2_IP(v, p, 8); }

//===----------------------------------------------------------------------===//
// Expanded MAC residual — 64-bit mul non-empty object
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_mul64_ss_ll
// IR: call {{.*}}@llvm.haydn.mul64
// ASM-LABEL: resid_mul64_ss_ll:
// ASM: {{mul64|MUL64|mul}}
// OBJ-LABEL: <resid_mul64_ss_ll>:
// OBJ: {{mul64|MUL64|mul}}
ae_int64 resid_mul64_ss_ll(ae_int64 a, ae_int64 b) {
  return AE_MUL64_SS_LL(a, b);
}

//===----------------------------------------------------------------------===//
// Peer pure residual — independent value/object beyond representatives
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_sub32s
// IR: call {{.*}}@llvm.haydn.x2sub32s
// ASM-LABEL: resid_sub32s:
// ASM: {{x2sub32s|sub32s}}
// OBJ-LABEL: <resid_sub32s>:
// OBJ: {{x2sub32s|sub32s}}
ae_int32x2 resid_sub32s(ae_int32x2 a, ae_int32x2 b) { return AE_SUB32S(a, b); }

// IR-LABEL: @resid_sub16
// IR: call {{.*}}@llvm.haydn.x4sub16
// ASM-LABEL: resid_sub16:
// ASM: x4sub16
// OBJ-LABEL: <resid_sub16>:
// OBJ: x4sub16
ae_int64 resid_sub16(void) {
  ae_int16x4 a = {3, 3, 3, 3};
  ae_int16x4 b = {1, 1, 1, 1};
  return __AE_TO_I64(AE_SUB16(a, b));
}

// IR-LABEL: @resid_sub16s
// IR: call {{.*}}@llvm.haydn.x4sub16s
// ASM-LABEL: resid_sub16s:
// ASM: {{x4sub16s|sub16s}}
// OBJ-LABEL: <resid_sub16s>:
// OBJ: {{x4sub16s|sub16s}}
ae_int16x4 resid_sub16s(ae_int16x4 a, ae_int16x4 b) { return AE_SUB16S(a, b); }

// IR-LABEL: @resid_srai32_one
// IR: call {{.*}}@llvm.haydn.x2sra32
// ASM-LABEL: resid_srai32_one:
// ASM: {{x2sra32|sra32|SRA}}
// OBJ-LABEL: <resid_srai32_one>:
// OBJ: {{x2sra32|sra32|SRA}}
ae_int32x2 resid_srai32_one(ae_int32x2 a) { return AE_SRAI32(a, 1); }

// IR-LABEL: @resid_srli32_one
// IR: call {{.*}}@llvm.haydn.x2srl32
// ASM-LABEL: resid_srli32_one:
// ASM: {{x2srl32|srl32|SRL}}
// OBJ-LABEL: <resid_srli32_one>:
// OBJ: {{x2srl32|srl32|SRL}}
ae_int32x2 resid_srli32_one(ae_int32x2 a) { return AE_SRLI32(a, 1); }

// IR-LABEL: @resid_min32
// IR: call {{.*}}@llvm.haydn.x2min32
// ASM-LABEL: resid_min32:
// ASM: {{x2min32|min32|MIN}}
// OBJ-LABEL: <resid_min32>:
// OBJ: {{x2min32|min32|MIN}}
ae_int32x2 resid_min32(ae_int32x2 a, ae_int32x2 b) { return AE_MIN32(a, b); }

// IR-LABEL: @resid_max32
// IR: call {{.*}}@llvm.haydn.
// ASM-LABEL: resid_max32:
// ASM: {{x2mov|x2slt|max32|MAX|movt|MOVT}}
// OBJ-LABEL: <resid_max32>:
// OBJ: {{x2mov|x2slt|max32|MAX|movt|MOVT}}
ae_int32x2 resid_max32(ae_int32x2 a, ae_int32x2 b) { return AE_MAX32(a, b); }

// IR-LABEL: @resid_maxabs32s
// IR: call {{.*}}@llvm.haydn.maxabs32s
// ASM-LABEL: resid_maxabs32s:
// ASM: {{maxabs32s|MAXABS|abs|ABS}}
// OBJ-LABEL: <resid_maxabs32s>:
// OBJ: {{maxabs32s|MAXABS|abs|ABS}}
ae_int32x2 resid_maxabs32s(ae_int32x2 a, ae_int32x2 b) {
  return AE_MAXABS32S(a, b);
}

// IR-LABEL: @resid_add64s
// IR: call {{.*}}@llvm.haydn.add64s
// ASM-LABEL: resid_add64s:
// ASM: {{add64s|ADD64S|add}}
// OBJ-LABEL: <resid_add64s>:
// OBJ: {{add64s|ADD64S|add}}
ae_int64 resid_add64s(ae_int64 a, ae_int64 b) { return AE_ADD64S(a, b); }

// IR-LABEL: @resid_abs64s
// IR: call {{.*}}@llvm.haydn.abs64s
// ASM-LABEL: resid_abs64s:
// ASM: {{abs64s|ABS64S|abs}}
// OBJ-LABEL: <resid_abs64s>:
// OBJ: {{abs64s|ABS64S|abs}}
ae_int64 resid_abs64s(ae_int64 a) { return AE_ABS64S(a); }

//===----------------------------------------------------------------------===//
// Peer memory residual — L16X4_IP / L32_IP non-empty object
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_l16x4_ip
// IR: load
// ASM-LABEL: resid_l16x4_ip:
// ASM: {{ld64|ldw|LDW|LD64|addi}}
// OBJ-LABEL: <resid_l16x4_ip>:
// OBJ: {{ld64|ldw|LDW|LD64|addi}}
ae_int16x4 resid_l16x4_ip(ae_int16x4 *p) {
  ae_int16x4 v;
  AE_L16X4_IP(v, p, 8);
  return v;
}

// IR-LABEL: @resid_s16x4_ip
// IR: store
// ASM-LABEL: resid_s16x4_ip:
// ASM: {{st64|sdw|SDW|ST64|addi}}
// OBJ-LABEL: <resid_s16x4_ip>:
// OBJ: {{st64|sdw|SDW|ST64|addi}}
void resid_s16x4_ip(ae_int16x4 v, ae_int16x4 *p) { AE_S16X4_IP(v, p, 8); }

// IR-LABEL: @resid_l32_ip
// IR: load
// ASM-LABEL: resid_l32_ip:
// ASM: {{ldw|LDW|ld32|LD32|s_lw|lw|addi}}
// OBJ-LABEL: <resid_l32_ip>:
// OBJ: {{ldw|LDW|ld32|LD32|s_lw|lw|addi}}
ae_int32x2 resid_l32_ip(ae_int32 *p) {
  ae_int32x2 v;
  AE_L32_IP(v, p, 4);
  return v;
}

//===----------------------------------------------------------------------===//
// Peer MAC residual — muls / mulafp non-empty object
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_muls32x2
// IR: call {{.*}}@llvm.haydn.x2muls32
// ASM-LABEL: resid_muls32x2:
// ASM: {{x2muls32|muls32|MULS}}
// OBJ-LABEL: <resid_muls32x2>:
// OBJ: {{x2muls32|muls32|MULS}}
ae_int64 resid_muls32x2(void) {
  ae_int64 acc = 0;
  ae_int32x2 a = {2, 3};
  ae_int32x2 b = {4, 5};
  return AE_MULS32X2(acc, a, b);
}

// IR-LABEL: @resid_mulafp32x2ras
// IR: call {{.*}}@llvm.haydn.ff2mula32rs
// ASM-LABEL: resid_mulafp32x2ras:
// ASM: {{ff2mula32rs|mula|MULA|fmul|FMUL}}
// OBJ-LABEL: <resid_mulafp32x2ras>:
// OBJ: {{ff2mula32rs|mula|MULA|fmul|FMUL}}
ae_int64 resid_mulafp32x2ras(void) {
  ae_int64 acc = 0;
  ae_int32x2 a = {0x40000000, 0x40000000};
  ae_int32x2 b = {0x40000000, 0x40000000};
  return AE_MULAFP32X2RAS(acc, a, b);
}

// IR-LABEL: @resid_mul16js
// IR: call {{.*}}@llvm.haydn.x4mjswap16s
// ASM-LABEL: resid_mul16js:
// ASM: {{x4mjswap16s|mjswap|MUL16JS}}
// OBJ-LABEL: <resid_mul16js>:
// OBJ: {{x4mjswap16s|mjswap|MUL16JS}}
ae_int16x4 resid_mul16js(ae_int16x4 a) { return AE_MUL16JS(a); }

//===----------------------------------------------------------------------===//
// Residual pure/memory/state seats for remaining TD-authored OracleId pins
// (independent host value / non-empty object; no silent empty body).
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_add32
// IR: call {{.*}}@llvm.haydn.x2add32
// ASM-LABEL: resid_add32:
// ASM: {{x2add32|add32|ADD32}}
// OBJ-LABEL: <resid_add32>:
// OBJ: {{x2add32|add32|ADD32}}
ae_int32x2 resid_add32(ae_int32x2 a, ae_int32x2 b) { return AE_ADD32(a, b); }

// IR-LABEL: @resid_max16
// IR: call {{.*}}@llvm.haydn.x4max16
// ASM-LABEL: resid_max16:
// ASM: {{x4max16|max16|MAX}}
// OBJ-LABEL: <resid_max16>:
// OBJ: {{x4max16|max16|MAX}}
ae_int16x4 resid_max16(ae_int16x4 a, ae_int16x4 b) { return AE_MAX16(a, b); }

// IR-LABEL: @resid_min16
// IR: call {{.*}}@llvm.haydn.x4min16
// ASM-LABEL: resid_min16:
// ASM: {{x4min16|min16|MIN}}
// OBJ-LABEL: <resid_min16>:
// OBJ: {{x4min16|min16|MIN}}
ae_int16x4 resid_min16(ae_int16x4 a, ae_int16x4 b) { return AE_MIN16(a, b); }

// IR-LABEL: @resid_mov_passthru
// IR: ret
// ASM-LABEL: resid_mov_passthru:
// OBJ-LABEL: <resid_mov_passthru>:
// OBJ: {{jalr|addi|xor}}
ae_int32x2 resid_mov_passthru(ae_int32x2 a) { return AE_MOV(a); }

// IR-LABEL: @resid_sel32_lh
// IR: call {{.*}}@llvm.haydn.x2sel32.lh
// ASM-LABEL: resid_sel32_lh:
// ASM: {{x2sel32_lh|sel32|SEL}}
// OBJ-LABEL: <resid_sel32_lh>:
// OBJ: {{x2sel32_lh|sel32|SEL}}
ae_int32x2 resid_sel32_lh(ae_int32x2 a, ae_int32x2 b) { return AE_SEL32_LH(a, b); }

// IR-LABEL: @resid_slaa32s_one
// IR: {{shl|select|icmp|call}}
// ASM-LABEL: resid_slaa32s_one:
// ASM: {{sll|SLA|slaa|x2sll|movt|MOVT|slt|SLT}}
// OBJ-LABEL: <resid_slaa32s_one>:
// OBJ: {{sll|SLA|slaa|x2sll|movt|MOVT|slt|SLT|addi}}
ae_int32x2 resid_slaa32s_one(ae_int32x2 a) { return AE_SLAA32S(a, 1); }

// IR-LABEL: @resid_cvtq56a32s
// IR: {{sext|shl|call}}
// ASM-LABEL: resid_cvtq56a32s:
// ASM: {{sext32t64|sll64|SLL|SEXT}}
// OBJ-LABEL: <resid_cvtq56a32s>:
// OBJ: {{sext32t64|sll64|SLL|SEXT}}
ae_int64 resid_cvtq56a32s(int32_t a) { return AE_CVTQ56A32S(a); }

// IR-LABEL: @resid_l32x2_x
// IR: load
// ASM-LABEL: resid_l32x2_x:
// ASM: {{ld64|ldw|LDW|LD64|d_ldw}}
// OBJ-LABEL: <resid_l32x2_x>:
// OBJ: {{ld64|ldw|LDW|LD64|d_ldw}}
ae_int32x2 resid_l32x2_x(const ae_int32x2 *p) { return AE_L32X2_X(p, 0); }

// IR-LABEL: @resid_s32x2_x
// IR: store
// ASM-LABEL: resid_s32x2_x:
// ASM: {{st64|sdw|SDW|ST64|d_sdw}}
// OBJ-LABEL: <resid_s32x2_x>:
// OBJ: {{st64|sdw|SDW|ST64|d_sdw}}
void resid_s32x2_x(ae_int32x2 v, ae_int32x2 *p) { AE_S32X2_X(v, p, 0); }

// IR-LABEL: @resid_s32_l_ip
// IR: store
// ASM-LABEL: resid_s32_l_ip:
// ASM: {{st32|sw|SW|ST32|s_sw|addi}}
// OBJ-LABEL: <resid_s32_l_ip>:
// OBJ: {{st32|sw|SW|ST32|s_sw|addi}}
void resid_s32_l_ip(ae_int32 v, ae_int32 *p) { AE_S32_L_IP(v, p, 4); }

// IR-LABEL: @resid_l32x2_xp
// IR: load
// ASM-LABEL: resid_l32x2_xp:
// ASM: {{ld64|ldw|LDW|LD64|d_ldw|addi}}
// OBJ-LABEL: <resid_l32x2_xp>:
// OBJ: {{ld64|ldw|LDW|LD64|d_ldw|addi}}
ae_int32x2 resid_l32x2_xp(ae_int32x2 *p) {
  ae_int32x2 v;
  AE_L32X2_XP(v, p, 0, 8);
  return v;
}

//===----------------------------------------------------------------------===//
// Authored residual peers beyond family reps: exact lane-select, peer memory,
// peer MAC, ae0 convert/shift, and pure maxabs seats.
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_sel32_hh
// IR: call {{.*}}@llvm.haydn.x2sel32.hh
// ASM-LABEL: resid_sel32_hh:
// ASM: {{x2sel32_hh|sel32|SEL}}
// OBJ-LABEL: <resid_sel32_hh>:
// OBJ: {{x2sel32_hh|sel32|SEL}}
ae_int32x2 resid_sel32_hh(ae_int32x2 a, ae_int32x2 b) { return AE_SEL32_HH(a, b); }

// IR-LABEL: @resid_sel32_ll
// IR: call {{.*}}@llvm.haydn.x2sel32.ll
// ASM-LABEL: resid_sel32_ll:
// ASM: {{x2sel32_ll|sel32|SEL}}
// OBJ-LABEL: <resid_sel32_ll>:
// OBJ: {{x2sel32_ll|sel32|SEL}}
ae_int32x2 resid_sel32_ll(ae_int32x2 a, ae_int32x2 b) { return AE_SEL32_LL(a, b); }

// IR-LABEL: @resid_sel32_hl
// IR: call {{.*}}@llvm.haydn.x2sel32.hl
// ASM-LABEL: resid_sel32_hl:
// ASM: {{x2sel32_hl|sel32|SEL}}
// OBJ-LABEL: <resid_sel32_hl>:
// OBJ: {{x2sel32_hl|sel32|SEL}}
ae_int32x2 resid_sel32_hl(ae_int32x2 a, ae_int32x2 b) { return AE_SEL32_HL(a, b); }

// IR-LABEL: @resid_maxabs16s
// IR: call {{.*}}@llvm.haydn.x4abs16s
// ASM-LABEL: resid_maxabs16s:
// ASM: {{x4abs16s|abs16|ABS}}
// OBJ-LABEL: <resid_maxabs16s>:
// OBJ: {{x4abs16s|abs16|ABS}}
ae_int16x4 resid_maxabs16s(ae_int16x4 a) { return AE_MAXABS16S(a); }

// IR-LABEL: @resid_cvt16x4
// IR: call {{.*}}@llvm.haydn.x4sat32t16
// ASM-LABEL: resid_cvt16x4:
// ASM: {{x4sat32t16|sat32|SAT}}
// OBJ-LABEL: <resid_cvt16x4>:
// OBJ: {{x4sat32t16|sat32|SAT}}
ae_int16x4 resid_cvt16x4(ae_int32x2 a, ae_int32x2 b) { return AE_CVT16X4(a, b); }

// IR-LABEL: @resid_mul64_ss_hh
// IR: call {{.*}}@llvm.haydn.mul64.ss.hh
// ASM-LABEL: resid_mul64_ss_hh:
// ASM: {{mul64_hh|mul64\.hh|MUL64}}
// OBJ-LABEL: <resid_mul64_ss_hh>:
// OBJ: {{mul64_hh|mul64\.hh|MUL64}}
ae_int64 resid_mul64_ss_hh(ae_int32x2 a, ae_int32x2 b) {
  return AE_MUL64_SS_HH(a, b);
}

// IR-LABEL: @resid_mula64_ss_ll
// IR: call {{.*}}@llvm.haydn.mula64.ss.ll
// ASM-LABEL: resid_mula64_ss_ll:
// ASM: {{mula64_ll|mula64\.ll|MULA64|mula}}
// OBJ-LABEL: <resid_mula64_ss_ll>:
// OBJ: {{mula64_ll|mula64\.ll|MULA64|mula}}
ae_int64 resid_mula64_ss_ll(ae_int64 acc, ae_int32x2 a, ae_int32x2 b) {
  return AE_MULA64_SS_LL(acc, a, b);
}

// IR-LABEL: @resid_s32x2_xp
// IR: store
// ASM-LABEL: resid_s32x2_xp:
// ASM: {{sdw|st64|ST64|d_sdw|addi}}
// OBJ-LABEL: <resid_s32x2_xp>:
// OBJ: {{sdw|st64|ST64|d_sdw|addi}}
void resid_s32x2_xp(ae_int32x2 v, ae_int32x2 *p) { AE_S32X2_XP(v, p, 0, 8); }

// IR-LABEL: @resid_l16x4_x
// IR: load
// ASM-LABEL: resid_l16x4_x:
// ASM: {{ldw|ld64|LDW|LD64|d_ldw}}
// OBJ-LABEL: <resid_l16x4_x>:
// OBJ: {{ldw|ld64|LDW|LD64|d_ldw}}
ae_int16x4 resid_l16x4_x(const ae_int16x4 *p) { return AE_L16X4_X(p, 0); }

// IR-LABEL: @resid_l32_xp
// IR: load
// ASM-LABEL: resid_l32_xp:
// ASM: {{lw|ld32|LD32|s_lw|addi}}
// OBJ-LABEL: <resid_l32_xp>:
// OBJ: {{lw|ld32|LD32|s_lw|addi}}
ae_int32 resid_l32_xp(ae_int32 *p) {
  ae_int32 v;
  AE_L32_XP(v, p, 0, 4);
  return v;
}

// IR-LABEL: @resid_slaa64s_one
// IR: {{shl|select|icmp|call|ashr}}
// ASM-LABEL: resid_slaa64s_one:
// ASM: {{sll|SLA|slaa|slli|SLLI|slt|SLT|movt|MOVT}}
// OBJ-LABEL: <resid_slaa64s_one>:
// OBJ: {{sll|SLA|slaa|slli|SLLI|slt|SLT|movt|MOVT|addi}}
ae_int64 resid_slaa64s_one(ae_int64 a) { return AE_SLAA64S(a, 1); }

//===----------------------------------------------------------------------===//
// Further authored residual peers: arithmetic shift, halfword mem, cross-MAC.
//===----------------------------------------------------------------------===//

// IR-LABEL: @resid_sraa32_one
// IR: {{ashr|lshr|call|shl}}
// ASM-LABEL: resid_sraa32_one:
// ASM: {{sra|SRA|x2sra|sraa}}
// OBJ-LABEL: <resid_sraa32_one>:
// OBJ: {{sra|SRA|x2sra|sraa}}
ae_int32x2 resid_sraa32_one(ae_int32x2 a) { return AE_SRAA32(a, 1); }

// IR-LABEL: @resid_sraa16_one
// IR: {{ashr|lshr|call|shl}}
// ASM-LABEL: resid_sraa16_one:
// ASM: {{sra|SRA|x4sra|sraa}}
// OBJ-LABEL: <resid_sraa16_one>:
// OBJ: {{sra|SRA|x4sra|sraa}}
ae_int16x4 resid_sraa16_one(ae_int16x4 a) { return AE_SRAA16(a, 1); }

// IR-LABEL: @resid_srai64_one
// IR: {{ashr|lshr|call|shl}}
// ASM-LABEL: resid_srai64_one:
// ASM: {{sra|SRA|sra64|SRAI}}
// OBJ-LABEL: <resid_srai64_one>:
// OBJ: {{sra|SRA|sra64|SRAI}}
ae_int64 resid_srai64_one(ae_int64 a) { return AE_SRAI64(a, 1); }

// IR-LABEL: @resid_l16_ip
// IR: load
// ASM-LABEL: resid_l16_ip:
// ASM: {{lh|lhu|lw|ld16|LH|LHWS|s_lh}}
// OBJ-LABEL: <resid_l16_ip>:
// OBJ: {{lh|lhu|lw|ld16|LH|LHWS|s_lh}}
ae_int16 resid_l16_ip(ae_int16 *p) {
  ae_int16 v;
  AE_L16_IP(v, p, 2);
  return v;
}

// IR-LABEL: @resid_s16x4_x
// IR: store
// ASM-LABEL: resid_s16x4_x:
// ASM: {{sdw|st64|ST64|d_sdw|sw|ST32}}
// OBJ-LABEL: <resid_s16x4_x>:
// OBJ: {{sdw|st64|ST64|d_sdw|sw|ST32}}
void resid_s16x4_x(ae_int16x4 v, ae_int16x4 *p) { AE_S16X4_X(v, p, 0); }

// IR-LABEL: @resid_l16x4_xp
// IR: load
// ASM-LABEL: resid_l16x4_xp:
// ASM: {{ldw|ld64|LDW|LD64|d_ldw|addi}}
// OBJ-LABEL: <resid_l16x4_xp>:
// OBJ: {{ldw|ld64|LDW|LD64|d_ldw|addi}}
ae_int16x4 resid_l16x4_xp(ae_int16x4 *p) {
  ae_int16x4 v;
  AE_L16X4_XP(v, p, 0, 8);
  return v;
}

// IR-LABEL: @resid_mul32x16_l0
// IR: call {{.*}}@llvm.haydn
// ASM-LABEL: resid_mul32x16_l0:
// ASM: {{mul|MUL|fmula|MULA}}
// OBJ-LABEL: <resid_mul32x16_l0>:
// OBJ: {{mul|MUL|fmula|MULA}}
ae_int64 resid_mul32x16_l0(ae_int32x2 a, ae_int16x4 b) {
  return AE_MUL32X16_L0(a, b);
}

// IR-LABEL: @resid_mula32x16_l0
// IR: call {{.*}}@llvm.haydn
// ASM-LABEL: resid_mula32x16_l0:
// ASM: {{mula|MULA|mul|MUL}}
// OBJ-LABEL: <resid_mula32x16_l0>:
// OBJ: {{mula|MULA|mul|MUL}}
ae_int64 resid_mula32x16_l0(ae_int64 acc, ae_int32x2 a, ae_int16x4 b) {
  return AE_MULA32X16_L0(acc, a, b);
}

//===----------------------------------------------------------------------===//
// Further residual pure peers (TD-authored OracleId + value/object seats).
//===----------------------------------------------------------------------===//

_Static_assert(HAYDN_COMPAT_TIER_AE_ABS32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_NEG32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_NEG64S == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLLI64 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SRLI64 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_MOV64 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_EQ64 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_ABS32, "emu.abs32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_NEG32, "emu.neg32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_NEG64S, "emu.neg64s") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLLI64, "emu.slli64") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SRLI64, "emu.srli64") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_MOV64, "emu.mov64") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_EQ64, "emu.eq64") == 0, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LT64 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_LE64 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLLA32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAA32 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_SLAI64 == HAYDN_COMPAT_EMULATED, "");
_Static_assert(HAYDN_COMPAT_TIER_AE_F32X2_SLAIS == HAYDN_COMPAT_EMULATED, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_LT64, "emu.lt64") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_LE64, "emu.le64") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLLA32, "emu.slla32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAA32, "emu.slaa32") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_SLAI64, "emu.slai64") == 0, "");
_Static_assert(__builtin_strcmp(HAYDN_AE_ORACLE_AE_F32X2_SLAIS, "softsat.f32x2_slais") == 0, "");

// IR-LABEL: @resid_abs32
// IR: call {{.*}}@llvm.haydn
// ASM-LABEL: resid_abs32:
// ASM: {{abs|x2abs|ABS}}
// OBJ-LABEL: <resid_abs32>:
// OBJ: {{abs|x2abs|ABS}}
ae_int32x2 resid_abs32(ae_int32x2 a) { return AE_ABS32(a); }

// IR-LABEL: @resid_neg32
// IR: call {{.*}}@llvm.haydn
// ASM-LABEL: resid_neg32:
// ASM: {{neg|x2neg|NEG}}
// OBJ-LABEL: <resid_neg32>:
// OBJ: {{neg|x2neg|NEG}}
ae_int32x2 resid_neg32(ae_int32x2 a) { return AE_NEG32(a); }

// IR-LABEL: @resid_neg64s_one
// IR: {{call|sub|xor}}
// ASM-LABEL: resid_neg64s_one:
// ASM: {{neg|NEG|sub|SUB}}
// OBJ-LABEL: <resid_neg64s_one>:
// OBJ: {{neg|NEG|sub|SUB}}
ae_int64 resid_neg64s_one(void) { return AE_NEG64S((ae_int64)1); }

// IR-LABEL: @resid_slli64_one
// IR: {{shl|call}}
// ASM-LABEL: resid_slli64_one:
// ASM: {{sll|SLL|slai|SLAI}}
// OBJ-LABEL: <resid_slli64_one>:
// OBJ: {{sll|SLL|slai|SLAI}}
ae_int64 resid_slli64_one(void) { return AE_SLLI64((ae_int64)1, 1); }

// IR-LABEL: @resid_srli64_one
// IR: {{lshr|call}}
// ASM-LABEL: resid_srli64_one:
// ASM: {{srl|SRL}}
// OBJ-LABEL: <resid_srli64_one>:
// OBJ: {{srl|SRL}}
ae_int64 resid_srli64_one(void) { return AE_SRLI64((ae_int64)2, 1); }

// IR-LABEL: @resid_mov64_const
// IR: ret i64 7
// ASM-LABEL: resid_mov64_const:
// OBJ-LABEL: <resid_mov64_const>:
// OBJ: addi32
ae_int64 resid_mov64_const(void) { return AE_MOV64(7); }

// IR-LABEL: @resid_eq64_true
// IR: ret i{{1|32|64}} {{1|true|-1}}
// ASM-LABEL: resid_eq64_true:
// OBJ-LABEL: <resid_eq64_true>:
// OBJ: {{addi32|seq|SEQ|beq|BNE|bne}}
int resid_eq64_true(void) { return AE_EQ64((ae_int64)3, (ae_int64)3) ? 1 : 0; }

// IR-LABEL: @resid_lt64_true
// IR: ret i{{1|32|64}} {{1|true|-1}}
// ASM-LABEL: resid_lt64_true:
// OBJ-LABEL: <resid_lt64_true>:
// OBJ: {{addi32|slt|SLT|blt|BLT|blt}}
int resid_lt64_true(void) { return AE_LT64((ae_int64)3, (ae_int64)5) ? 1 : 0; }

// IR-LABEL: @resid_le64_eq
// IR: ret i{{1|32|64}} {{1|true|-1}}
// ASM-LABEL: resid_le64_eq:
// OBJ-LABEL: <resid_le64_eq>:
// OBJ: {{addi32|sle|SLE|slt|SLT|beq|BNE}}
int resid_le64_eq(void) { return AE_LE64((ae_int64)3, (ae_int64)3) ? 1 : 0; }

// IR-LABEL: @resid_slai64_one
// IR: {{call .*@llvm.haydn.sll64|shl|ret i64}}
// ASM-LABEL: resid_slai64_one:
// ASM: {{sll|SLL|slli|SLLI|addi}}
// OBJ-LABEL: <resid_slai64_one>:
// OBJ: {{sll|SLL|slli|SLLI|addi}}
ae_int64 resid_slai64_one(void) { return AE_SLAI64((ae_int64)1, 1); }

// IR-LABEL: @resid_slla32_one
// IR: {{shl|call|ashr}}
// ASM-LABEL: resid_slla32_one:
// ASM: {{sll|SLL|x2sll|slla}}
// OBJ-LABEL: <resid_slla32_one>:
// OBJ: {{sll|SLL|x2sll|slla}}
ae_int32x2 resid_slla32_one(ae_int32x2 a) { return AE_SLLA32(a, 1); }

// IR-LABEL: @resid_slaa32_one
// IR: call {{.*}} @llvm.haydn.x2sra32
// IR: call {{.*}} @llvm.haydn.movad32.low
int resid_slaa32_one(void) {
  return (int)__AE_S32_LO(AE_SLAA32(((ae_int32x2){1, 0}), 1));
}

// IR-LABEL: @resid_f32x2_slais_one
// IR: {{shl|select|icmp|call}}
// ASM-LABEL: resid_f32x2_slais_one:
// ASM: {{sll|SLA|slaa|x2sll|movt|MOVT|slt|SLT}}
// OBJ-LABEL: <resid_f32x2_slais_one>:
// OBJ: {{sll|SLA|slaa|x2sll|movt|MOVT|slt|SLT|addi}}
ae_int32x2 resid_f32x2_slais_one(ae_int32x2 a) { return AE_F32X2_SLAIS(a, 1); }

/* =====================================================================
 * Empty-body dual-64 permanent quarantine — fail-closed (no silent body)
 * ===================================================================== */

#if defined(TEST_ADD64X2_STRICT)
// STRICT: __haydn_ae_unsupported_AE_ADD64X2_
ae_int64 empty_body_add64x2_(ae_int64 a, ae_int64 b) {
  return AE_ADD64X2_(a, b);
}
// STRICT: __haydn_ae_unsupported_AE_ADD64X2_vector
ae_int64 empty_body_add64x2_vector(ae_int64 a, ae_int64 b) {
  return AE_ADD64X2_vector(a, b);
}
// STRICT: __haydn_ae_unsupported_AE_MULFC24RA
ae_f24x2 empty_body_mulfc24ra(ae_f24x2 a, ae_f24x2 b) {
  return AE_MULFC24RA(a, b);
}
// STRICT: silent-wrong map removed): AE_CMUL32_F2
void empty_body_cmul32_f2(ae_int32x2 *d0, ae_int32x2 *d1,
                          ae_int32x2 a, ae_int32x2 b) {
  AE_CMUL32_F2(*d0, *d1, a, b);
}
// STRICT: silent-wrong map removed): AE_CMUL32S_F2
void empty_body_cmul32s_f2(ae_int32x2 *d0, ae_int32x2 *d1,
                           ae_int32x2 a, ae_int32x2 b) {
  AE_CMUL32S_F2(*d0, *d1, a, b);
}
#endif
