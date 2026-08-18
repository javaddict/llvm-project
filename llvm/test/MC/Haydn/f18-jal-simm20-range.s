# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=IN_RANGE=1 %s \
# RUN:   | FileCheck --check-prefix=IN %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=OOR_POS=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-POS %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=OOR_NEG=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-NEG %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=ODD=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=ODD %s

# REGRESSION TEST: F18 — JAL literal imm20 must not silently truncate.
#
# Bug: getCallTargetOpValue encoded JAL immediates as Imm & 0xFFFFF with
# no isInt<20> check. Product JAL members use getSImmOpValueXStepWide
# (brtarget_wide_i20 / FIXUP_HAYDN_WIDE_CallSImm20) and masked the same
# way. jal lr, 524288 (2^19) encoded as 0 instead of diagnosing.
#
# Fix: isInt<20> + reportError on both EncoderMethods (same law).
# In-range window is even values in [-524288, 524286] (signed 20-bit and
# MinBundleAddressAlignBytes=2). If this regresses, OOR encodes and
# FileCheck misses "jal offset out of range".

.ifdef IN_RANGE
# IN: jal{{.*}}encoding:
	jal lr, 524286
# IN: jal{{.*}}encoding:
	jal lr, -524288
.endif

.ifdef OOR_POS
	jal lr, 524288
# OOR-POS: error: jal offset out of range
.endif

.ifdef OOR_NEG
	jal lr, -524289
# OOR-NEG: error: jal offset out of range
.endif

.ifdef ODD
	jal lr, 2
	jal lr, 1
# ODD: error: jal offset must be 2-byte aligned
.endif
