# REQUIRES: haydn-registered-target
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCChecker.cpp --check-prefix=DR8

# D1.85(a): parse-time DR read ceiling is golden 8R, not the retired 7R.
# Behavioral 8-vs-9 pin is HaydnMCCheckerTest (haydnCheckParsedBundleRegs).

# Sequential vs HaydnMCChecker.cpp: named PortModel ceiling first, then the
# diagnostic string (split across two C++ literals; FileCheck is forward-only).
# DR8: HAYDN_DR_READ_PORTS
# DR8: DR 8R/3W
# DR8-NOT: DRR > 7
# DR8-NOT: DRR > 8
