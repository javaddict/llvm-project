# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=IN_RANGE=1 %s \
# RUN:   | FileCheck --check-prefix=IN %s
# RUN: not llvm-mc -triple=haydn-unknown-elf --defsym=OOR=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=OOR %s

# Role: parse — %hi12 constant payload is range-checked from RelocFieldInfo
# FieldSize (12) after the Hi12 transform, not a hardcoded parser 12.

# In-range: 1572864 → (Val+0x80000)>>20 = 2 fits unsigned 12.
# Out-of-range: 0xFFF80000 → (Val+0x80000)>>20 = 0x1000 does not fit.

.ifdef IN_RANGE
# IN: lui{{.*}}encoding:
    lui r1, %hi12(1572864)
.endif

.ifdef OOR
    lui r1, %hi12(0xFFF80000)
# OOR: error: HI12 value does not fit in the LUI ext field
.endif
