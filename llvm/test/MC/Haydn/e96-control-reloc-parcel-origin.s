# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readelf -r %t.o | FileCheck %s --check-prefix=RELOC
# REQUIRES: haydn-registered-target
#
# Product FieldLsb is absolute parcel bits with r_offset = parcel origin.
# E3 I12/RI12 windows start at bit 23 (byte 2). A mid-parcel reloc offset
# makes lld compute P = PC+2, so linked B/JAL targets land 2 bytes early
# (not an exact 12-byte record). E2 and E3 control relocs must sit at
# EncodedBytes boundaries (0, 12, 24).

.text
e3_br:
  { bne_w r1, r2, ext_br; nop; nop }
e3_jal:
  { jal lr, ext_jal; nop; nop }
e2_br:
  { bnez_w r3, ext_br2; nop }

# RELOC-DAG: 00000000 {{.*}} R_HAYDN_WIDE_BranchSImm12_RI {{.*}} ext_br
# RELOC-DAG: 0000000c {{.*}} R_HAYDN_WIDE_CallSImm20 {{.*}} ext_jal
# RELOC-DAG: 00000018 {{.*}} R_HAYDN_WIDE_BranchSImm12 {{.*}} ext_br2
