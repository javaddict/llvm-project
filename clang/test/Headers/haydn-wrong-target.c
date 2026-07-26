// RUN: not %clang_cc1 %s -triple x86_64-unknown-linux-gnu -fsyntax-only 2>&1 \
// RUN:   | FileCheck %s --check-prefix=TYPES
// RUN: not %clang_cc1 %s -triple aarch64-unknown-linux-gnu -fsyntax-only 2>&1 \
// RUN:   | FileCheck %s --check-prefix=TYPES
// RUN: not %clang_cc1 %s -DHAYDN_H -triple x86_64-unknown-linux-gnu \
// RUN:   -fsyntax-only 2>&1 | FileCheck %s --check-prefix=TYPES
// RUN: not %clang_cc1 %s -DHAYDN_DSP -triple x86_64-unknown-linux-gnu \
// RUN:   -fsyntax-only 2>&1 | FileCheck %s --check-prefix=TYPES
//
// C3.2 / G-CAPI-FEATURE: public Haydn headers fail closed off-target
// (xmmintrin.h peer). haydn.h / haydn_dsp.h include haydn_types.h, so the
// same architecture #error fires for the full public stack.
//
// TYPES: This header is only meant to be used on Haydn architecture

#if defined(HAYDN_DSP)
#include <haydn_dsp.h>
#elif defined(HAYDN_H)
#include <haydn.h>
#else
#include <haydn_types.h>
#endif
