# REQUIRES: haydn-registered-target
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnAsmPrinter.cpp --check-prefix=A
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnMCInstLower.cpp --check-prefix=D
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnFinalizeBundle.cpp --check-prefix=F

# Compiler-origin silent repairs must fatal: missing-row E2 default,
# child-count row override, and dangling-MBB rewrite onto the parent.

# A: refuse E2
# A: E3 override
# D: refuse dangling-block repair
# F: silent repair
