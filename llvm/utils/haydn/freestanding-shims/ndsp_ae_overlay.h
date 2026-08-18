/* NatureDSP compile overlay included after haydn_dsp.h.
 *
 * haydn_dsp.h AE_MULAF16X4SS / AE_MULSF16X4SS write back with the destination
 * C type. Original vec_dot16x16_fast uses ae_f32x2 accumulators. Keep the
 * same haydn_x4mula16s / haydn_x4muls16s bits and assign through typeof.
 *
 * Official haydn_x2int32 is GNU vector_size(8). Clang rejects NatureDSP
 * `ae_int32x2 v = 0` and `AE_ROUND16X4F32SASYM(0, v)` (scalar-to-GNU-vector).
 * Re-alias ae_int32x2 / ae_f32x2 to Clang ext_vector after haydn_dsp.h so
 * those kernel sites compile; storage stays 8-byte <2 x i32>. Peer: AIE
 * OpenCL int2 (clctypes.h:60). Bag-cast helpers go through __AE_TO_I64 /
 * __AE_AS_V2. GNU vector_size and ext_vector <2 x i32> are compatible, so
 * haydn_dsp.h lists only haydn_x2int32 in _Generic (covers this alias).
 * Include this header after haydn_dsp.h (not before): a first-include type
 * swap breaks haydn_dsp.h bag casts.
 *
 * This is header hygiene for the approved product_library_pin matrix.
 * It does not split haydn_dsp.h and does not change the documented
 * 32-bit sat host oracle for vec_dot16.
 */
#ifndef HAYDN_NDSP_AE_OVERLAY_H
#define HAYDN_NDSP_AE_OVERLAY_H

#ifdef AE_MULAF16X4SS
#undef AE_MULAF16X4SS
#define AE_MULAF16X4SS(acc_hi, acc_lo, a, b)                                   \
  do {                                                                         \
    haydn_dpair_t _r = haydn_x4mula16s(__AE_TO_I64(acc_hi),                    \
                                       __AE_TO_I64(acc_lo), (a), (b));         \
    (acc_hi) = (__typeof__(acc_hi))(_r.hi);                                    \
    (acc_lo) = (__typeof__(acc_lo))(_r.lo);                                    \
  } while (0)
#endif

#ifdef AE_MULSF16X4SS
#undef AE_MULSF16X4SS
#define AE_MULSF16X4SS(acc_hi, acc_lo, a, b)                                   \
  do {                                                                         \
    haydn_dpair_t _r = haydn_x4muls16s(__AE_TO_I64(acc_hi),                    \
                                       __AE_TO_I64(acc_lo), (a), (b));         \
    (acc_hi) = (__typeof__(acc_hi))(_r.hi);                                    \
    (acc_lo) = (__typeof__(acc_lo))(_r.lo);                                    \
  } while (0)
#endif

/* Clang ext_vector accepts scalar 0; GNU vector_size does not. */
#define ae_int32x2 haydn_ndsp_i32x2
#define ae_f32x2 haydn_ndsp_i32x2

#ifdef AE_MOVF24X2_FROMINT32X2
#undef AE_MOVF24X2_FROMINT32X2
#define AE_MOVF24X2_FROMINT32X2(a) ((ae_f24x2)__AE_TO_I64(a))
#endif
#ifdef AE_MOVF24X2_FROMF32X2
#undef AE_MOVF24X2_FROMF32X2
#define AE_MOVF24X2_FROMF32X2(a) ((ae_f24x2)__AE_TO_I64(a))
#endif
#ifdef AE_MOVINT32X2_FROMF24X2
#undef AE_MOVINT32X2_FROMF24X2
#define AE_MOVINT32X2_FROMF24X2(a) ((ae_int32x2)__AE_AS_V2(a))
#endif
#ifdef AE_MOVINT32X2_FROMINT64
#undef AE_MOVINT32X2_FROMINT64
#define AE_MOVINT32X2_FROMINT64(a) ((ae_int32x2)__AE_AS_V2(a))
#endif
#ifdef AE_MOVF32X2_FROMINT32
#undef AE_MOVF32X2_FROMINT32
#define AE_MOVF32X2_FROMINT32(a) ((ae_f32x2)__AE_AS_V2(a))
#endif
#ifdef AE_MOVF32X2_FROMINT32X2
#undef AE_MOVF32X2_FROMINT32X2
#define AE_MOVF32X2_FROMINT32X2(a) ((ae_f32x2)__AE_AS_V2(a))
#endif
#ifdef AE_MOVF32X2_FROMF64
#undef AE_MOVF32X2_FROMF64
#define AE_MOVF32X2_FROMF64(a) ((ae_f32x2)__AE_AS_V2(a))
#endif
#ifdef AE_MOVF32X2_FROMF16X4
#undef AE_MOVF32X2_FROMF16X4
#define AE_MOVF32X2_FROMF16X4(a) ((ae_f32x2)__AE_AS_V2(a))
#endif
#ifdef AE_MOVINT24X2_FROMF32X2
#undef AE_MOVINT24X2_FROMF32X2
#define AE_MOVINT24X2_FROMF32X2(a) ((ae_int24x2)__AE_TO_I64(a))
#endif

#ifdef __AE_ROUND16X4F32SASYM_2A
#undef __AE_ROUND16X4F32SASYM_2A
#define __AE_ROUND16X4F32SASYM_2A(a, b)                                        \
  haydn_round16x4_after_trunca(__AE_AS_V2(a), __AE_AS_V2(b), 16)
#endif
#ifdef __AE_ROUND16X4F32SASYM_3A
#undef __AE_ROUND16X4F32SASYM_3A
#define __AE_ROUND16X4F32SASYM_3A(a, b, s)                                     \
  haydn_round16x4_after_trunca(__AE_AS_V2(a), __AE_AS_V2(b), (s))
#endif

#ifdef AE_L16X2M_IU
#undef AE_L16X2M_IU
#define AE_L16X2M_IU(dst, ptr, inc)                                            \
  do {                                                                         \
    (dst) = (__typeof__(dst))__AE_AS_V2((int64_t)*(ae_int32 *)(ptr));          \
    (ptr) = (__typeof__(ptr))((char *)(ptr) + (inc));                          \
  } while (0)
#endif
#ifdef AE_L16X2M_XU
#undef AE_L16X2M_XU
#define AE_L16X2M_XU(dst, ptr, offs) AE_L16X2M_IU(dst, ptr, offs)
#endif

#ifdef AE_L32X2_IP
#undef AE_L32X2_IP
#define AE_L32X2_IP(dst, ptr, inc)                                             \
  do {                                                                         \
    uint32_t __w0 = *(const uint32_t *)(void *)(ptr);                          \
    uint32_t __w1 = *(const uint32_t *)((const char *)(void *)(ptr) + 4);      \
    (dst) = (__typeof__(dst))__AE_AS_V2((uint64_t)__w1 |                       \
                                        ((uint64_t)__w0 << 32));               \
    (ptr) = (__typeof__(ptr))((char *)(ptr) + (inc));                          \
  } while (0)
#endif
#ifdef AE_S32X2_IP
#undef AE_S32X2_IP
#define AE_S32X2_IP(src, ptr, inc)                                             \
  do {                                                                         \
    uint64_t __u = (uint64_t)__AE_TO_I64(src);                                 \
    uint32_t __hi = (uint32_t)(__u >> 32);                                     \
    uint32_t __lo = (uint32_t)__u;                                             \
    *(uint32_t *)(void *)(ptr) = __hi;                                         \
    *(uint32_t *)((char *)(void *)(ptr) + 4) = __lo;                           \
    (ptr) = (__typeof__(ptr))((char *)(ptr) + (inc));                          \
  } while (0)
#endif

#ifdef AE_MULAAD32X16_H1_L0
#undef AE_MULAAD32X16_H1_L0
#define AE_MULAAD32X16_H1_L0(acc, d, c)                                        \
  do {                                                                         \
    haydn_dr64_t __c32 = haydn_i16x4_pair_as_i32x2(__AE_TO_I64(c), 1, 0);      \
    (acc) = haydn_mula64_ss_hh((acc), __AE_TO_I64(d), __c32);                  \
    (acc) = haydn_mula64_ss_ll((acc), __AE_TO_I64(d), __c32);                  \
  } while (0)
#endif
#ifdef AE_MULAAD32X16_H3_L2
#undef AE_MULAAD32X16_H3_L2
#define AE_MULAAD32X16_H3_L2(acc, d, c)                                        \
  do {                                                                         \
    haydn_dr64_t __c32 = haydn_i16x4_pair_as_i32x2(__AE_TO_I64(c), 3, 2);      \
    (acc) = haydn_mula64_ss_hh((acc), __AE_TO_I64(d), __c32);                  \
    (acc) = haydn_mula64_ss_ll((acc), __AE_TO_I64(d), __c32);                  \
  } while (0)
#endif

#ifdef AE_MULFD32X16X2_FIR_HH
#undef AE_MULFD32X16X2_FIR_HH
#define AE_MULFD32X16X2_FIR_HH(q0, q1, d0, d1, c)                              \
  do {                                                                         \
    haydn_dr64_t __d0 = __AE_TO_I64(d0), __d1 = __AE_TO_I64(d1);               \
    haydn_dr64_t __cv = __AE_TO_I64(c);                                        \
    haydn_dr64_t __c0 = haydn_q15_lane_as_q31_dup(__cv, 0);                    \
    haydn_dr64_t __c1 = haydn_q15_lane_as_q31_dup(__cv, 1);                    \
    (q0) = haydn_ff2mula32rs_hh((ae_int64)0, __AE_TO_I64(__d0),                 \
                                __AE_TO_I64(__c0));                            \
    (q0) = haydn_ff2mula32rs_ll((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c1));   \
    (q1) = haydn_ff2mula32rs_ll((ae_int64)0, __AE_TO_I64(__d0),                 \
                                __AE_TO_I64(__c0));                            \
    (q1) = haydn_ff2mula32rs_hh((q1), __AE_TO_I64(__d1), __AE_TO_I64(__c1));   \
  } while (0)
#endif
#ifdef AE_MULFD32X16X2_FIR_HL
#undef AE_MULFD32X16X2_FIR_HL
#define AE_MULFD32X16X2_FIR_HL(q0, q1, d0, d1, c)                              \
  do {                                                                         \
    haydn_dr64_t __d0 = __AE_TO_I64(d0), __d1 = __AE_TO_I64(d1);               \
    haydn_dr64_t __cv = __AE_TO_I64(c);                                        \
    haydn_dr64_t __c2 = haydn_q15_lane_as_q31_dup(__cv, 2);                    \
    haydn_dr64_t __c3 = haydn_q15_lane_as_q31_dup(__cv, 3);                    \
    (q0) = haydn_ff2mula32rs_hh((ae_int64)0, __AE_TO_I64(__d0),                 \
                                __AE_TO_I64(__c2));                            \
    (q0) = haydn_ff2mula32rs_ll((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c3));   \
    (q1) = haydn_ff2mula32rs_ll((ae_int64)0, __AE_TO_I64(__d0),                 \
                                __AE_TO_I64(__c2));                            \
    (q1) = haydn_ff2mula32rs_hh((q1), __AE_TO_I64(__d1), __AE_TO_I64(__c3));   \
  } while (0)
#endif
#ifdef AE_MULAFD32X16X2_FIR_HH
#undef AE_MULAFD32X16X2_FIR_HH
#define AE_MULAFD32X16X2_FIR_HH(q0, q1, d0, d1, c)                             \
  do {                                                                         \
    haydn_dr64_t __d0 = __AE_TO_I64(d0), __d1 = __AE_TO_I64(d1);               \
    haydn_dr64_t __cv = __AE_TO_I64(c);                                        \
    haydn_dr64_t __c0 = haydn_q15_lane_as_q31_dup(__cv, 0);                    \
    haydn_dr64_t __c1 = haydn_q15_lane_as_q31_dup(__cv, 1);                    \
    (q0) = haydn_ff2mula32rs_hh((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c0));   \
    (q0) = haydn_ff2mula32rs_ll((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c1));   \
    (q1) = haydn_ff2mula32rs_ll((q1), __AE_TO_I64(__d0), __AE_TO_I64(__c0));   \
    (q1) = haydn_ff2mula32rs_hh((q1), __AE_TO_I64(__d1), __AE_TO_I64(__c1));   \
  } while (0)
#endif
#ifdef AE_MULAFD32X16X2_FIR_HL
#undef AE_MULAFD32X16X2_FIR_HL
#define AE_MULAFD32X16X2_FIR_HL(q0, q1, d0, d1, c)                             \
  do {                                                                         \
    haydn_dr64_t __d0 = __AE_TO_I64(d0), __d1 = __AE_TO_I64(d1);               \
    haydn_dr64_t __cv = __AE_TO_I64(c);                                        \
    haydn_dr64_t __c2 = haydn_q15_lane_as_q31_dup(__cv, 2);                    \
    haydn_dr64_t __c3 = haydn_q15_lane_as_q31_dup(__cv, 3);                    \
    (q0) = haydn_ff2mula32rs_hh((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c2));   \
    (q0) = haydn_ff2mula32rs_ll((q0), __AE_TO_I64(__d0), __AE_TO_I64(__c3));   \
    (q1) = haydn_ff2mula32rs_ll((q1), __AE_TO_I64(__d0), __AE_TO_I64(__c2));   \
    (q1) = haydn_ff2mula32rs_hh((q1), __AE_TO_I64(__d1), __AE_TO_I64(__c3));   \
  } while (0)
#endif

#ifdef AE_MOVAD32_H
#undef AE_MOVAD32_H
#define AE_MOVAD32_H(a) ((ae_int32)haydn_movad32_h(__AE_TO_I64(a)))
#endif
#ifdef AE_MOVAD32_L
#undef AE_MOVAD32_L
#define AE_MOVAD32_L(a) ((ae_int32)haydn_movad32_l(__AE_TO_I64(a)))
#endif
#ifdef AE_MOVINT64_FROMINT32X2
#undef AE_MOVINT64_FROMINT32X2
#define AE_MOVINT64_FROMINT32X2(a) ((ae_int64)__AE_TO_I64(a))
#endif
#ifdef AE_MOVINT32X2_FROMINT64
#undef AE_MOVINT32X2_FROMINT64
#define AE_MOVINT32X2_FROMINT64(a) ((ae_int32x2)__AE_AS_V2(a))
#endif

#ifdef __AE_S16_0_XP_3A
#undef __AE_S16_0_XP_3A
#define __AE_S16_0_XP_3A(src, ptr, offs)                                       \
  do {                                                                         \
    *(ae_int16 *)((char *)(ptr) + (offs)) =                                    \
        (ae_int16)(uint16_t)(uint64_t)__AE_TO_I64(src);                        \
    (ptr) = (__typeof__(ptr))((char *)(ptr) + (offs));                         \
  } while (0)
#endif
#ifdef __AE_S16_0_XP_4A
#undef __AE_S16_0_XP_4A
#define __AE_S16_0_XP_4A(src, ptr, offs, inc)                                  \
  do {                                                                         \
    *(ae_int16 *)((char *)(ptr) + (offs)) =                                    \
        (ae_int16)(uint16_t)(uint64_t)__AE_TO_I64(src);                        \
    (ptr) = (__typeof__(ptr))((char *)(ptr) + (inc));                          \
  } while (0)
#endif

#ifdef __AE_S32X2_XC_4A
#undef __AE_S32X2_XC_4A
#define __AE_S32X2_XC_4A(src, ptr, offs, cbr_sel)                              \
  do {                                                                         \
    haydn_dr64_t __s = haydn_ae_f32x2_mem_to_reg(__AE_TO_I64(src));            \
    void *__np = haydn_sdw_cb_imm(__s, (ptr), (cbr_sel), (offs) >> 3);         \
    (ptr) = (__typeof__(ptr))__np;                                             \
  } while (0)
#endif
#ifdef __AE_L32X2_XC_4A
#undef __AE_L32X2_XC_4A
#define __AE_L32X2_XC_4A(dst, ptr, offs, cbr_sel)                              \
  do {                                                                         \
    haydn_cb_ld_t __r = haydn_ldw_cb_imm((ptr), (cbr_sel), (offs) >> 3);       \
    (dst) = (__typeof__(dst))__AE_AS_V2(                                       \
        haydn_ae_f32x2_mem_to_reg((haydn_dr64_t)__r.data));                    \
    (ptr) = (__typeof__(ptr))__r.new_ptr;                                      \
  } while (0)
#endif

#ifdef AE_MULAAFD32X16_H1_L0
#undef AE_MULAAFD32X16_H1_L0
#define AE_MULAAFD32X16_H1_L0(acc, a, b)                                       \
  ((acc) = haydn_mulafd32x16x2_fir_hl(__AE_TO_I64(acc), __AE_TO_I64(a),        \
                                      __AE_TO_I64(b)))
#endif
#ifdef AE_MULAAFD32X16_H3_L2
#undef AE_MULAAFD32X16_H3_L2
#define AE_MULAAFD32X16_H3_L2(acc, a, b)                                       \
  ((acc) = haydn_mulafd32x16x2_fir_hh(__AE_TO_I64(acc), __AE_TO_I64(a),        \
                                      __AE_TO_I64(b)))
#endif
#ifdef AE_MULASFD32X16_H1_L0
#undef AE_MULASFD32X16_H1_L0
#define AE_MULASFD32X16_H1_L0(acc, a, b)                                       \
  ((acc) = haydn_f2mulss32rs_hhll((acc), __AE_TO_I64(a), __AE_TO_I64(b)))
#endif
#ifdef AE_MULASFD32X16_H3_L2
#undef AE_MULASFD32X16_H3_L2
#define AE_MULASFD32X16_H3_L2(acc, a, b)                                       \
  ((acc) = haydn_f2mulss32rs_hhll((acc), __AE_TO_I64(a), __AE_TO_I64(b)))
#endif
#ifdef AE_MULAAFD32X16_H2_L3
#undef AE_MULAAFD32X16_H2_L3
#define AE_MULAAFD32X16_H2_L3(acc, a, b)                                       \
  ((acc) = haydn_f2mulaa32rs_hhll((acc), __AE_TO_I64(a), __AE_TO_I64(b)))
#endif
#ifdef AE_MULAAFD32X16_H0_L1
#undef AE_MULAAFD32X16_H0_L1
#define AE_MULAAFD32X16_H0_L1(acc, a, b)                                       \
  ((acc) = haydn_f2mulaa32rs_hhll((acc), __AE_TO_I64(a), __AE_TO_I64(b)))
#endif
#ifdef AE_MULZAAFD32X16_H2_L3_2A
#undef AE_MULZAAFD32X16_H2_L3_2A
#define AE_MULZAAFD32X16_H2_L3_2A(a, b)                                        \
  haydn_f2mulaa32rs_hhll((ae_int64)0, __AE_TO_I64(a), __AE_TO_I64(b))
#endif
#ifdef AE_MULZAAFD32X16_H2_L3_3A
#undef AE_MULZAAFD32X16_H2_L3_3A
#define AE_MULZAAFD32X16_H2_L3_3A(acc, a, b)                                   \
  ((acc) = haydn_f2mulaa32rs_hhll((ae_int64)0, __AE_TO_I64(a), __AE_TO_I64(b)))
#endif
#undef AE_MULZASFD32X16_H3_L2_2A
#define AE_MULZASFD32X16_H3_L2_2A(a, b)                                        \
  haydn_f2mulss32rs_hhll((ae_int64)0, __AE_TO_I64(a), __AE_TO_I64(b))
#undef AE_MULZASFD32X16_H3_L2_3A
#define AE_MULZASFD32X16_H3_L2_3A(acc, a, b)                                   \
  ((void)(acc), haydn_f2mulss32rs_hhll((ae_int64)0, __AE_TO_I64(a),            \
                                       __AE_TO_I64(b)))

#endif /* HAYDN_NDSP_AE_OVERLAY_H */
