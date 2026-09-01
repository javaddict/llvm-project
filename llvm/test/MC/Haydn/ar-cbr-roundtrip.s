# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d %t.o | FileCheck %s --check-prefix=DIS
# REQUIRES: haydn-registered-target

# Golden v2_2 AR_CBR family admission: type 101 on LOADSTORE0 entry0 in
# both packet entries. Wire shape per golden mapping rows:
#   loads    d_l*hwua_cb_post ar_sel, cbr_sel, rtd, rs   (tied rs writeback)
#   stores   d_s*hwua_cb_post ar_sel, cbr_sel, rtd, rs   (data rtd is a use)
#   pointer  pl*wwua_cb_post ar_sel, cbr_sel, rs
#   barrier  wbarwua_cb      ar_sel, cbr_sel, rs
# ar_sel/cbr_sel must appear on the wire (ar_sel 0 != 1, cbr_sel 0 != 1)
# or the circular-buffer selection collapses; imm fields differing across
# packets proves the encoder did not zero them.

{ d_ltwua_cb_post 0, 0, d0, r1 }
# ENC: d_ltwua_cb_post
# ENC: encoding: [{{.*}}]
# ENC-NOT: encoding: [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
{ d_ltwua_cb_post 1, 0, d0, r1 }
{ d_ltwua_cb_post 0, 1, d0, r1 }
{ d_lqhwua_cb_post 0, 0, d1, r2 }
# DIS: d_ltwua_cb_post{{.*}}0{{.*}}0{{.*}}d0{{.*}}r1
# DIS: d_ltwua_cb_post{{.*}}1{{.*}}0{{.*}}d0{{.*}}r1
# DIS: d_ltwua_cb_post{{.*}}0{{.*}}1{{.*}}d0{{.*}}r1
# DIS: d_lqhwua_cb_post{{.*}}0{{.*}}0{{.*}}d1{{.*}}r2

{ d_stwua_cb_post 0, 0, d0, r1 }
{ d_sqhwua_cb_post 1, 1, d2, r3 }
# DIS: d_stwua_cb_post{{.*}}0{{.*}}0{{.*}}d0{{.*}}r1
# DIS: d_sqhwua_cb_post{{.*}}1{{.*}}1{{.*}}d2{{.*}}r3

{ pltwwua_cb_post 0, 0, r1 }
{ plqhwua_cb_post 1, 0, r2 }
{ wbarwua_cb 0, 1, r3 }
# DIS: pltwwua_cb_post{{.*}}0{{.*}}0{{.*}}r1
# DIS: plqhwua_cb_post{{.*}}1{{.*}}0{{.*}}r2
# DIS: wbarwua_cb{{.*}}0{{.*}}1{{.*}}r3
