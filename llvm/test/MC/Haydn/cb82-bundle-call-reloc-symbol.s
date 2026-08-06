# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readelf -r -s %t.o | FileCheck %s
#
# : Bundle128 BUNDLE-wrapped jal must emit R_HAYDN_WIDE_CallSImm20
# against the *named* UND symbol (not symbol index 0).
#
# CHECK: R_HAYDN_WIDE_CallSImm20{{.*}} main
# CHECK: GLOBAL DEFAULT UND main

  { jal lr, main; nop; nop }
