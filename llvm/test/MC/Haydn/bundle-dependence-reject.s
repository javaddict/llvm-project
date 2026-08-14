# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o 2>&1 | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: verifier — dependence / unit-port co-issue seat (G-TEST-EVIDENCE).
# Two stores cannot share one Format-E product parcel (LS unit capacity /
# placement). Object path must refuse sequential E2 singleton split rather
# than invent a legal multi-cycle encoding for an illegal co-issue.
#
# Companion entry-count seat: bundle-canadd-reject.s (>3 entries).
# Seat inventory: Inputs/CORRUPTION-MATRIX.txt (dependence).
# Note: this path is a non-abort LLVM ERROR (use plain `not`, not `--crash`).

# CHECK: Format E one-parcel placement failed
# CHECK-SAME: ST32
# CHECK-SAME: refuse sequential E2 singleton split

{ st32 r1, r2, 0; st32 r3, r4, 0 }
