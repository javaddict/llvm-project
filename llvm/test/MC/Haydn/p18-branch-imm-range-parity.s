# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=IN_RANGE=1 %s \
# RUN:   | FileCheck --check-prefix=IN %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=OOR_POS=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-POS %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=OOR_NEG=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-NEG %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=ODD=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=ODD %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=JAL_OOR_POS=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=JAL-OOR-POS %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=JAL_OOR_NEG=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=JAL-OOR-NEG %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=JAL_ODD=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=JAL-ODD %s

# JAL and cond-branch immediates share one generated-width law
# (getSImmOpValueXStepWide N/Shift/IsSigned; AIE getSImmOpValueXStep
# AIEBaseMCCodeEmitter.h:127-150). Cond-branch: signed 12-bit even-byte
# window [-2048, 2046]. JAL: signed 20-bit even-byte window
# [-524288, 524286]. Do not silently mask.

.ifdef IN_RANGE
# IN: beq{{.*}}encoding:
	beq r1, r2, 2046
# IN: beq{{.*}}encoding:
	beq r1, r2, -2048
# IN: jal{{.*}}encoding:
	jal lr, 524286
# IN: jal{{.*}}encoding:
	jal lr, -524288
.endif

.ifdef OOR_POS
	beq r1, r2, 2048
# OOR-POS: error: immediate operand value is out of range
.endif

.ifdef OOR_NEG
	beq r1, r2, -2049
# OOR-NEG: error: immediate operand value is out of range
.endif

.ifdef ODD
	beq r1, r2, 1
# ODD: error: offset must be 2-byte aligned
.endif

.ifdef JAL_OOR_POS
	jal lr, 524288
# JAL-OOR-POS: error: jal offset out of range
.endif

.ifdef JAL_OOR_NEG
	jal lr, -524289
# JAL-OOR-NEG: error: jal offset out of range
.endif

.ifdef JAL_ODD
	jal lr, 1
# JAL-ODD: error: jal offset must be 2-byte aligned
.endif
