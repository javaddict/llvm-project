// RUN: not %clang_cc1 -triple haydn-unknown-elf -fsyntax-only -ffreestanding \
// RUN:   %s 2>&1 | FileCheck %s --check-prefix=GENERIC
// RUN: %clang_cc1 -triple haydn-unknown-elf -target-cpu haydn -fsyntax-only \
// RUN:   -ffreestanding %s
//
// REQUIRES: haydn-registered-target
//
// REGRESSION TEST: T-DSP12 default-CPU haydn_dsp.h surface trap.
//
// Product CPU policy (HaydnGeneric.td / HaydnTargetInfo): empty -mcpu and
// -mcpu=generic are the densify baseline (agu+hwloop). SIMD / circular-
// buffer / bit-reversed stay opt-in on -mcpu=haydn. Silently defaulting
// the driver to -mcpu=haydn would contradict that policy.
//
// haydn_dsp.h fail-closes on generic (agu+hwloop) with one #error before
// including generated haydn.h (peer: xmmintrin.h:13-15). Do not silently
// default -mcpu=haydn. -target-cpu haydn still compiles.
//
// GENERIC: haydn_dsp.h needs target feature simd
// GENERIC-NOT: always_inline

#include <haydn_dsp.h>
