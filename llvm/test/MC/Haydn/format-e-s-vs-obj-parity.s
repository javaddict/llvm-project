# RUN: %python %S/../../../utils/haydn/check_mc_s_vs_obj_parity.py --self-test
# RUN: %python %S/../../../utils/haydn/check_mc_s_vs_obj_parity.py --llvm-src %S/../../../..
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: T-MC6 / M8 — generated round-trip + .s-vs-obj byte parity.
#
# Bug class: CB-95 (ld32_reg matched in asm / -show-encoding but object
# parcels were idle/all-zero). R3 only pins mnemonic *text* in MC tests;
# encode vs object-byte divergence was a manual probe recipe.
#
# Test design: the Python harness assembles the generated catalog
# (format-e-mnemonic-roundtrip.s). It requires llvm-mc -show-encoding
# bytes == llvm-mc -filetype=obj .text, each parcel sized from generated
# FormatEEncodedBytes (HaydnGenFormatERecords.inc, not a scattered width
# literal), no all-zero parcel, and objdump to print each encodable
# mnemonic. --self-test uses a non-product BundleBits width so a hardcoded
# product parcel size cannot pass, and XOR-flips object bytes to prove a
# broken encoder fails. The live RUN repeats that mutation canary on the
# real catalog object.
#
# If this regresses: assembler text and ELF .text can silently diverge
# again, or all-zero Format E parcels can ship as a false PASS.
#
# Role: harness — object byte contract over the generated Format E catalog
# (NOT a golden-vector invent and NOT a disassembler change).
# Peer: format-e-mnemonic-roundtrip.s (R3 name presence).
