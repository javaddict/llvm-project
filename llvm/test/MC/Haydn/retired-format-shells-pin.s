# RUN: %python %S/../../../utils/haydn/check_retired_format_shells.py --self-test
# RUN: %python %S/../../../utils/haydn/check_retired_format_shells.py --llvm-src %S/../../../..
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: retired 16-bit compressed TableGen shells stay deleted.
#
# Bug: HaydnInst16 / HaydnInstrInfoC.td / hypo_encoding / encoding_manual /
# BUNDLE128 / HaydnDClassOpcodes / HaydnMCFlags / HaydnFlexLayout /
# EW_16Bit / BUNDLE_16BIT_TAG are pre-Format-E product residue.
# Product is one 12-byte Format E parcel. If those identifiers return as
# live Target .td/.cpp/.h (class HaydnInst16, include HaydnInstrInfoC.td,
# or a non-tombstone citation), MC can grow a 16-bit all-zero NOP path
# or a deleted DClass encoder.
#
# Test design: Python pin scans llvm/lib/Target/Haydn *.{td,cpp,h}.
# Hard-fail include HaydnInstrInfoC and class HaydnInst16. Tombstone
# comments that name the ban are allowed. Peer: format-e-s-vs-obj-parity.s.
