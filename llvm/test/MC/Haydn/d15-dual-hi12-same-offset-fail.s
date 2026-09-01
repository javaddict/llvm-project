# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf %s \
# RUN:   --defsym=EXT=1 -o /dev/null 2>&1 | FileCheck %s --check-prefix=EXT
# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf %s \
# RUN:   --defsym=LOCAL=1 -o /dev/null 2>&1 | FileCheck %s --check-prefix=LOCAL
# RUN: not llvm-mc -triple=haydn-unknown-elf -show-encoding %s \
# RUN:   --defsym=RESOLVED=1 -o /dev/null 2>&1 | FileCheck %s --check-prefix=RESOLVED
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s --defsym=UNIQUE=1 \
# RUN:   -o %t.uniq.o
# RUN: llvm-readobj -r %t.uniq.o | FileCheck %s --check-prefix=UNIQUE

# Dual symbolic HI12 at one r_offset: two LUI I12 fields share the parcel
# origin, so lld would patch one window twice and the other never. D1.17:
# unique non-default LUI sites emit ENTRY-QUALIFIED kinds (typed window;
# no sniff); the D1.5 wall still refuses duplicates by baseKindFor, so
# two LUI fixups at two qualified entries of one parcel still refuse.
# Hand-asm only. report_fatal_error(GenCrashDiag=false) exits 1 (plain
# `not`, not abort).

.ifdef EXT
	{ lui r1, g0; lui r2, g1 }
# EXT: dual HI12
# EXT: same r_offset
.endif

.ifdef LOCAL
.text
_start:
	{ lui r1, loc0; lui r2, loc1 }
loc0:
	{ nop; nop }
loc1:
	{ nop; nop }
# LOCAL: dual HI12
# LOCAL: same r_offset
.endif

.ifdef RESOLVED
	{ lui r1, %hi12(0xa2004); lui r2, %hi12(0xb2004) }
# RESOLVED: dual HI12
# RESOLVED: same r_offset
.endif

.ifdef UNIQUE
	{ nop; lui r3, g0; nop }
	{ nop; nop; lui r4, g1 }
# UNIQUE: R_HAYDN_HI12 g0
# UNIQUE: R_HAYDN_HI12 g1
.endif
