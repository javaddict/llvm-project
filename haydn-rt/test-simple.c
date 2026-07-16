//===-- test-simple.c - Simple Haydn baremetal test ------------------ c ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Minimal test program for Haydn baremetal runtime.
// Tests that crt0, linker script, and basic compilation work.
//
//===----------------------------------------------------------------------===//

int main(void) {
    return 42;
}
