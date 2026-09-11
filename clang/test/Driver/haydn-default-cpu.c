// UNSUPPORTED: system-windows
// REQUIRES: haydn-registered-target
//
// Product processor identity for haydn-unknown-elf is -mtune=haydn.
// Empty -mcpu names generic. generic and haydn are the same full ISA
// (agu + circular-buffer + bit-reversed + hwloop + simd). There is no
// product feature-off CPU. -dM output is sorted.
//
// Companion cc1 pins: Preprocessor/haydn-predefines.c and
// Headers/haydn-dsp-default-cpu-simd-trap.c.

// Default: no -mcpu / no -mtune → -target-cpu generic, -tune-cpu haydn.
// RUN: %clang --target=haydn-unknown-elf -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEFAULT-CC1
// -tune-cpu is emitted from addClangTargetOptions (before -target-cpu).
// DEFAULT-CC1-DAG: "-tune-cpu" "haydn"
// DEFAULT-CC1-DAG: "-target-cpu" "generic"
// DEFAULT-CC1-NOT: "-target-cpu" "haydn"

// RUN: %clang --target=haydn-unknown-elf -E -dM -ffreestanding %s \
// RUN:   | FileCheck %s --check-prefix=DEFAULT-DM
// DEFAULT-DM: #define __HAYDN_CPU_GENERIC__ 1
// DEFAULT-DM-NOT: #define __HAYDN_CPU_HAYDN__
// DEFAULT-DM: #define __HAYDN_FEATURE_AGU__ 1
// DEFAULT-DM: #define __HAYDN_FEATURE_BIT_REVERSED__ 1
// DEFAULT-DM: #define __HAYDN_FEATURE_CIRCULAR_BUFFER__ 1
// DEFAULT-DM: #define __HAYDN_FEATURE_HWLOOP__ 1
// DEFAULT-DM: #define __HAYDN_FEATURE_SIMD__ 1
// DEFAULT-DM: #define __HAYDN_TUNE_HAYDN__ 1
// DEFAULT-DM-NOT: #define __HAYDN_TUNE_GENERIC__

// Explicit -mtune=generic keeps the generic CPU spelling; ISA stays full.
// RUN: %clang --target=haydn-unknown-elf -mtune=generic -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=TUNE-GENERIC-CC1
// TUNE-GENERIC-CC1-DAG: "-target-cpu" "generic"
// TUNE-GENERIC-CC1-DAG: "-tune-cpu" "generic"
// TUNE-GENERIC-CC1-NOT: "-target-cpu" "haydn"

// RUN: %clang --target=haydn-unknown-elf -mtune=generic -E -dM -ffreestanding %s \
// RUN:   | FileCheck %s --check-prefix=TUNE-GENERIC-DM
// TUNE-GENERIC-DM-DAG: #define __HAYDN_CPU_GENERIC__ 1
// TUNE-GENERIC-DM-DAG: #define __HAYDN_TUNE_GENERIC__ 1
// TUNE-GENERIC-DM-DAG: #define __HAYDN_FEATURE_SIMD__ 1
// TUNE-GENERIC-DM-NOT: #define __HAYDN_TUNE_HAYDN__

// Explicit -mcpu=generic is a spelling alias of the full ISA; tune stays haydn.
// RUN: %clang --target=haydn-unknown-elf -mcpu=generic -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=GENERIC-CC1
// GENERIC-CC1-DAG: "-target-cpu" "generic"
// GENERIC-CC1-DAG: "-tune-cpu" "haydn"
// GENERIC-CC1-NOT: "-target-cpu" "haydn"

// RUN: %clang --target=haydn-unknown-elf -mcpu=generic -E -dM -ffreestanding %s \
// RUN:   | FileCheck %s --check-prefix=GENERIC-DM
// GENERIC-DM: #define __HAYDN_CPU_GENERIC__ 1
// GENERIC-DM-NOT: #define __HAYDN_CPU_HAYDN__
// GENERIC-DM: #define __HAYDN_FEATURE_AGU__ 1
// GENERIC-DM: #define __HAYDN_FEATURE_BIT_REVERSED__ 1
// GENERIC-DM: #define __HAYDN_FEATURE_CIRCULAR_BUFFER__ 1
// GENERIC-DM: #define __HAYDN_FEATURE_HWLOOP__ 1
// GENERIC-DM: #define __HAYDN_FEATURE_SIMD__ 1
// GENERIC-DM: #define __HAYDN_TUNE_HAYDN__ 1

// -mcpu=haydn is the other full-ISA spelling; default tune remains haydn.
// RUN: %clang --target=haydn-unknown-elf -mcpu=haydn -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=HAYDN-CC1
// HAYDN-CC1-DAG: "-target-cpu" "haydn"
// HAYDN-CC1-DAG: "-tune-cpu" "haydn"
// HAYDN-CC1-NOT: "-target-cpu" "generic"

// RUN: %clang --target=haydn-unknown-elf -mcpu=haydn -E -dM -ffreestanding %s \
// RUN:   | FileCheck %s --check-prefix=HAYDN-DM
// HAYDN-DM-NOT: #define __HAYDN_CPU_GENERIC__
// HAYDN-DM: #define __HAYDN_CPU_HAYDN__ 1
// HAYDN-DM: #define __HAYDN_FEATURE_AGU__ 1
// HAYDN-DM: #define __HAYDN_FEATURE_BIT_REVERSED__ 1
// HAYDN-DM: #define __HAYDN_FEATURE_CIRCULAR_BUFFER__ 1
// HAYDN-DM: #define __HAYDN_FEATURE_HWLOOP__ 1
// HAYDN-DM: #define __HAYDN_FEATURE_SIMD__ 1
// HAYDN-DM: #define __HAYDN_TUNE_HAYDN__ 1

void f(void) {}
