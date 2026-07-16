/*
 * Freestanding xtensa/config/core-isa.h shim for Haydn port.
 *
 * The original NatureDSP common.h gates code paths on XCHAL_* capability
 * macros defined by the Tensilica core-isa.h. On Haydn we provide the
 * subset those guards reference. All AE_ semantics come from haydn_dsp.h
 * (pulled in via xt_hifi3.h below).
 */
#ifndef HAYDN_FREESTANDING_XTENSA_CORE_ISA_H
#define HAYDN_FREESTANDING_XTENSA_CORE_ISA_H

/* Advertise a HiFi3-class core so the NatureDSP code paths that expect
 * AE_ intrinsics are taken. VFPU is off (Haydn is soft-float). */
#ifndef XCHAL_SW_VERSION
#define XCHAL_SW_VERSION 1411000
#endif
#define XCHAL_HAVE_HIFI3       1
#define XCHAL_HAVE_HIFI3_VFPU  0
#define XCHAL_HAVE_NSA         1
#define XCHAL_HAVE_FP          0
#define XCHAL_HAVE_HIFI2       0
#define XCHAL_HAVE_HIFI1       0

#endif /* HAYDN_FREESTANDING_XTENSA_CORE_ISA_H */
