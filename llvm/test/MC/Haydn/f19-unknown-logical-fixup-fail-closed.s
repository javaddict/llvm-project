# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=KNOWN=1 %s \
# RUN:   | FileCheck --check-prefix=KNOWN %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=UNKNOWN=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=UNKNOWN %s

# REGRESSION TEST: F19 — unknown logicals must not default FIXUP_HAYDN_32.
#
# Bug: getExprFixupKind fell through to FIXUP_HAYDN_32 for any logical
# not in the typed list. That kind patches 4 absolute bytes at Offset=0
# and clobbers the Format E header (indicator 111). Two prior bugs were
# this shape (LUI member → 32, SET_HWLOOP → 32).
#
# Fix: unknown logical returns no kind; getMachineOpValue reportError
# and does not emit FIXUP_HAYDN_32. Do not invent a new reloc kind.
# slli32 takes uimm5 via getMachineOpValue (no EncoderMethod); a symbol
# is accepted at parse (non-constant expr) and must fail closed at encode.
# Known LUI still gets HI12 (positive control).
#
# If this regresses to the 32-bit default, UNKNOWN assembles and the
# object carries R_HAYDN_32 over the parcel header.

.ifdef KNOWN
	lui r1, foo
# KNOWN: lui{{.*}}foo
# KNOWN: fixup A - offset: 0, value: foo, kind: FIXUP_HAYDN_HI12
.endif

.ifdef UNKNOWN
	slli32 r1, r2, unknown_sym
# UNKNOWN: error: no typed fixup kind for symbolic operand
# UNKNOWN-NOT: kind: FIXUP_HAYDN_32
.endif
