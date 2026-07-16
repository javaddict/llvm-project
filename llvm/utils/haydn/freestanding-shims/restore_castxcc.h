/* Restore HiFi-compatible castxcc after NatureDSP common.h.
 *
 * haydn_dsp.h defines castxcc as an LVALUE form:
 *   (*(t **)&(p))
 * so post-inc load/store macros (AE_L*IP / AE_LA*_IP / AE_SA*_IP) can write
 * back through the cast and advance the underlying pointer (D185).
 *
 * NatureDSP include_private/common.h redefines:
 *   #define castxcc(type_,ptr) (type_ *)(ptr)
 * which is a non-lvalue temporary — any post-inc assignment fails to compile
 * or (if the macro dropped the advance) silently freezes the stream pointer.
 *
 * Include this header AFTER NatureDSP private headers (or as a trailing
 * -include) so castxcc stays lvalue for the rest of the translation unit.
 */
#pragma once

#undef castxcc
#define castxcc(t, p) (*(t **)&(p))
