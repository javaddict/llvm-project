# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o 2>&1 | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: verifier — dependence / unit-port co-issue seat (G-TEST-EVIDENCE).
# Two stores cannot share one Format-E product parcel (LS unit capacity /
# placement). Parse-time checker refuses the pack (Hexagon MCChecker analog)
# rather than reaching encode sequential-E2 split.
#
# Companion entry-count seat: bundle-canadd-reject.s (>3 entries).
# Seat inventory: Inputs/CORRUPTION-MATRIX.txt (dependence).
# Note: this path is a non-abort LLVM ERROR (use plain `not`, not `--crash`).

# CHECK: error: incorrect bundle: unit injectivity

{ st32 r1, r2, 0; st32 r3, r4, 0 }
