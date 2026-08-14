# RUN: not llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o 2>&1 | FileCheck %s
# REQUIRES: haydn-registered-target

# REGRESSION TEST: `.quad sym` must diagnose, never abort.
#
# Bug: HaydnELFObjectWriter::getRelocType mapped FK_Data_8 through
# llvm_unreachable("64-bit data relocations not supported on 32-bit Haydn").
# Assembling `.quad external_sym` aborted llvm-mc (SIGABRT / exit 134) instead
# of a normal error. Constant `.quad <imm>` is still legal raw data (see
# encoding-48bit-mode3.s); only a 64-bit *relocation* is unsupported.
#
# Fix: reportError + R_HAYDN_NONE. If this regresses to llvm_unreachable,
# `not --crash` fails because the process aborts. If it silently emits
# R_HAYDN_32 / R_HAYDN_NONE without a diagnostic, FileCheck fails.

        .data
        .quad   external_sym

# CHECK: error: 64-bit data relocations not supported on 32-bit Haydn
