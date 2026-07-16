/* Overlay for NatureDSP include_private/common.h.
 *
 * After including the real common.h (via include_next), restore castxcc to
 * the HiFi/Haydn LVALUE form so AE_*_IP post-increment macros can advance
 * the underlying pointer (D185). NatureDSP's definition is a non-lvalue
 * (type*)(ptr) temporary, which either fails to compile on post-inc writeback
 * or silently freezes streams when macros drop the advance.
 */
#pragma once

#include_next "common.h"

#undef castxcc
#define castxcc(t, p) (*(t **)&(p))
