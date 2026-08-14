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
// haydn_dsp.h includes generated haydn.h, whose always_inline SIMD
// wrappers call gated builtins. Plain triple therefore fails with the
// existing Sema feature diagnostic (not a silent empty header). This
// test pins that the diagnostic exists. Feature-gating wrapper bodies
// in haydn_dsp.h is a single-owner residual (do not edit that header
// here). Residual: -fsyntax-only -include haydn_dsp.h at the plain
// triple still emits many feature errors, not one wrapper diagnostic.
//
// GENERIC: needs target feature simd

#include <haydn_dsp.h>
