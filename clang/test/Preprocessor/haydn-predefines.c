// RUN: %clang_cc1 -E -dM -triple haydn-unknown-elf -target-cpu generic %s \
// RUN:   | FileCheck %s -check-prefix=GENERIC
// RUN: %clang_cc1 -E -dM -triple haydn-unknown-elf -target-cpu haydn %s \
// RUN:   | FileCheck %s -check-prefix=FULL
// RUN: %clang_cc1 -E -dM -triple haydn-unknown-elf -target-cpu generic \
// RUN:   -target-feature +simd %s \
// RUN:   | FileCheck %s -check-prefix=PLUS_SIMD
// RUN: %clang_cc1 -E -dM -triple haydn-unknown-elf -target-cpu haydn \
// RUN:   -target-feature -simd -target-feature -circular-buffer \
// RUN:   -target-feature -bit-reversed %s \
// RUN:   | FileCheck %s -check-prefix=STRIPPED
//
// C3.2 / G-CAPI-FEATURE: __HAYDN_ARCH__, CPU id, and semantic ISA capability
// macros. Gate only agu|circular-buffer|bit-reversed|hwloop|simd — never
// bundle FormatID / slot / AltDesc. -dM output is sorted; checks follow that.

// generic = agu + hwloop (HaydnGeneric.td product baseline)
// GENERIC-DAG: #define __ELF__ 1
// GENERIC-DAG: #define __HAYDN_32__ 1
// GENERIC-DAG: #define __HAYDN_ARCH__ 1
// GENERIC-DAG: #define __HAYDN_CPU_GENERIC__ 1
// GENERIC-DAG: #define __HAYDN_FEATURE_AGU__ 1
// GENERIC-DAG: #define __HAYDN_FEATURE_HWLOOP__ 1
// GENERIC-DAG: #define __HAYDN_LE__ 1
// GENERIC-DAG: #define __HAYDN_SOFT_FLOAT__ 1
// GENERIC-DAG: #define __HAYDN__ 1
// GENERIC-DAG: #define __SOFTFP__ 1
// GENERIC-DAG: #define __haydn_32__ 1
// GENERIC-DAG: #define __haydn_LE__ 1
// GENERIC-DAG: #define __haydn__ 1
// GENERIC-NOT: #define __HAYDN_CPU_HAYDN__
// GENERIC-NOT: #define __HAYDN_FEATURE_BIT_REVERSED__
// GENERIC-NOT: #define __HAYDN_FEATURE_CIRCULAR_BUFFER__
// GENERIC-NOT: #define __HAYDN_FEATURE_SIMD__
// GENERIC-NOT: FormatID
// GENERIC-NOT: AltDesc
// GENERIC-NOT: __HAYDN_BUNDLE
// GENERIC-NOT: __HAYDN_SLOT

// haydn = all five ISA features
// FULL-DAG: #define __HAYDN_ARCH__ 1
// FULL-DAG: #define __HAYDN_CPU_HAYDN__ 1
// FULL-DAG: #define __HAYDN_FEATURE_AGU__ 1
// FULL-DAG: #define __HAYDN_FEATURE_BIT_REVERSED__ 1
// FULL-DAG: #define __HAYDN_FEATURE_CIRCULAR_BUFFER__ 1
// FULL-DAG: #define __HAYDN_FEATURE_HWLOOP__ 1
// FULL-DAG: #define __HAYDN_FEATURE_SIMD__ 1
// FULL-DAG: #define __HAYDN__ 1
// FULL-NOT: #define __HAYDN_CPU_GENERIC__
// FULL-NOT: FormatID
// FULL-NOT: AltDesc

// +simd on generic enables SIMD without CB/BREV
// PLUS_SIMD-DAG: #define __HAYDN_CPU_GENERIC__ 1
// PLUS_SIMD-DAG: #define __HAYDN_FEATURE_AGU__ 1
// PLUS_SIMD-DAG: #define __HAYDN_FEATURE_HWLOOP__ 1
// PLUS_SIMD-DAG: #define __HAYDN_FEATURE_SIMD__ 1
// PLUS_SIMD-NOT: #define __HAYDN_FEATURE_BIT_REVERSED__
// PLUS_SIMD-NOT: #define __HAYDN_FEATURE_CIRCULAR_BUFFER__

// -simd/-circular-buffer/-bit-reversed on haydn leaves agu+hwloop
// STRIPPED-DAG: #define __HAYDN_CPU_HAYDN__ 1
// STRIPPED-DAG: #define __HAYDN_FEATURE_AGU__ 1
// STRIPPED-DAG: #define __HAYDN_FEATURE_HWLOOP__ 1
// STRIPPED-NOT: #define __HAYDN_FEATURE_BIT_REVERSED__
// STRIPPED-NOT: #define __HAYDN_FEATURE_CIRCULAR_BUFFER__
// STRIPPED-NOT: #define __HAYDN_FEATURE_SIMD__
