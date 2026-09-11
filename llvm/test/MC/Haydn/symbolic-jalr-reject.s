# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -triple=haydn-unknown-elf --defsym=IDENT=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=IDENT %s
# RUN: not llvm-mc -triple=haydn-unknown-elf --defsym=SPEC=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=SPEC %s
# RUN: not llvm-mc -triple=haydn-unknown-elf --defsym=BUNDLE=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=BUNDLE %s
# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding --defsym=LITERAL=1 %s \
# RUN:   | FileCheck --check-prefix=LITERAL %s

# ISA-69: jalr / jalr_w with a symbolic/relocatable third operand
# (identifier, specifier, or non-absolute MCExpr) must refuse. Absolute
# integer immediates, including 0 / odd / negative, still encode.

.ifdef IDENT
	jalr r1, r2, ext_sym
# IDENT: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
# IDENT: refusing silent PC-relative R_HAYDN_JALRSImm12
.endif

.ifdef SPEC
	jalr r1, r2, %lo20(ext_sym)
# SPEC: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
# SPEC: refusing silent PC-relative R_HAYDN_JALRSImm12
.endif

.ifdef BUNDLE
	{ nop; jalr r1, r2, ext_sym }
# BUNDLE: Haydn symbolic JALR is unsupported (ISA-69: no golden relocation base)
# BUNDLE: refusing silent PC-relative R_HAYDN_JALRSImm12
.endif

.ifdef LITERAL
	jalr r1, r2, 0
	jalr_w r3, r4, 0
# LITERAL: jalr{{.*}}encoding:
# LITERAL: jalr{{.*}}encoding:
.endif
