/*
 * Freestanding xtensa/tie/xt_hifi3.h shim for Haydn port.
 *
 * The original TIE header declares every AE_* intrinsic as a compiler
 * builtin. On Haydn we map all of them to the compatibility layer in
 * haydn_dsp.h (which composes them from __haydn_* intrinsics).
 *
 * Including this header brings the full AE_* surface into scope, exactly
 * as the HiFi3 TIE header would.
 */
#ifndef HAYDN_FREESTANDING_XTENSA_TIE_XT_HIFI3_H
#define HAYDN_FREESTANDING_XTENSA_TIE_XT_HIFI3_H

#include "haydn_dsp.h"

#endif /* HAYDN_FREESTANDING_XTENSA_TIE_XT_HIFI3_H */
