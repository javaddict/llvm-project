# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d -s --triple=haydn-unknown-elf %t.o | FileCheck %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_POS=1 %s -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-POS %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=OOR_NEG=1 %s -o /dev/null 2>&1 | FileCheck --check-prefix=OOR-NEG %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=MISALIGN=1 %s -o /dev/null 2>&1 | FileCheck --check-prefix=ALIGN %s

# Role: object — fail-closed residual for SET_HWLOOP offset range tables.
# Format E EncodedBytes=12: all layout gaps are whole parcels (no 4/16-byte
# residual .space that cannot be filled with product NOP).
# Positive: Off1 field=60 (body +240), Off2 field=63 (end +252, one-past body).
# Negative OOR/misalign paths exercise applyFixup fail-closed only.

# OOR-POS: relocation offset out of range
# OOR-NEG: relocation offset out of range
# ALIGN: mis-aligned relocation target

# SET resolves Off1 field=60 (byte dump 240) / Off2 field=63 (byte dump 252).
# Disassembler prints byte distances for BundleSim hwloop_scale contract.
# .space residual is zero-fill (not product idle) and may print as <unknown>.
# CHECK: set_hwloop_f2{{.*}}0, 240, 252, r1


.text
.globl test_hwloop_div4_bounds
# Format E max power-of-two align is 4 (parcel 12 has only 4 as pow2 divisor).
.balign 4

# ---- Positive Off1 near-max: body at +240 (field = 60) is in uimm6÷4. ----
# After SET (12 B) need 228 B = 19 parcels. Body one parcel; exclusive end
# one-past at +252 (field = 63).
pos_in_range:
    set_hwloop_f2_w 0, body_ok, end_ok, r1
    .space 228
body_ok:
    { add32 r1, r2, r3 }
end_ok:
    { add32 r4, r5, r6 }

.ifdef OOR_POS
# Positive just past Off1 range: body at +264 → field = 66 > uimm6 max 63.
# 264 is Format E parcel-aligned (22*12).
pos_out_of_range:
    set_hwloop_f2_w 0, body_bad, end_bad, r1
    .space 252
body_bad:
    { add32 r1, r2, r3 }
end_bad:
    { add32 r4, r5, r6 }
.endif

.ifdef OOR_NEG
# Negative / reverse layout is out of unsigned range (Off1 cannot encode
# a body that starts before the SET_HWLOOP parcel).
.balign 4
body_before:
    { add32 r1, r2, r3 }
end_before:
    { add32 r4, r5, r6 }
    .space 36
neg_out_of_range:
    set_hwloop_f2_w 0, body_before, end_before, r1
.endif

.ifdef MISALIGN
# Off1/Off2 require 4-byte alignment (Align=4). A 2-byte-only gap fails closed.
# Use .byte so MC does not attempt multi-byte product NOP fill for the gap.
misaligned:
    set_hwloop_f2_w 0, body_mis, end_mis, r1
    .byte 0, 0
body_mis:
    { add32 r1, r2, r3 }
    .byte 0, 0
end_mis:
    { add32 r4, r5, r6 }
.endif
