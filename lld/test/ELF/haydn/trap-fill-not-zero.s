# REQUIRES: haydn
# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf a.s -o a.o
# RUN: ld.lld -T pad.ld a.o -o out
# RUN: llvm-readobj -S out | FileCheck %s --check-prefix=SEC
# RUN: llvm-readobj -x .text out | FileCheck %s --check-prefix=HEX
# RUN: llvm-readobj -x .text out > dump.txt
# RUN: %python check_pad.py dump.txt

# REGRESSION TEST: executable trap/fill must not be all-zero Format E bytes.
#
# Bug: lld/ELF/Arch/Haydn.cpp set trapInstr = {0,0,0,0}. All-zero is not a
# Format E bundle (indicator must be 111). OutputSection::getFiller uses
# trapInstr for SHF_EXECINSTR gaps, so a linker-script `. = . + 12` pad in
# .text planted an invalid all-zero parcel.
#
# Fix: trapInstr is the first 4 bytes of canonicalFullSlotIdleParcel() (same
# generated idle as nopInstrs). Generic LLD trapInstr is 4 bytes; a 12-byte
# gap is therefore idle-derived, never all-zero. check_pad.py asserts the
# 12-byte pad (bytes 12..24 of .text) is not 12×0x00. If trapInstr regresses
# to zeros, that assert fires. Do not pin 4-byte tiling vs a future
# whole-parcel nopInstrFill of the same idle bytes.

# SEC: Name: .text
# SEC: Size: 24

# HEX: Hex dump of section '.text':

#--- a.s
        .section .text
        .globl _start
        .type _start, @function
_start:
        nop
        .size _start, .-_start

#--- pad.ld
SECTIONS {
  .text 0x10000 : {
    *(.text)
    . = . + 12;
  }
}

#--- check_pad.py
import sys

text = open(sys.argv[1]).read()
marker = "Hex dump of section '.text':"
if marker not in text:
    sys.exit("missing .text hex dump")
body = text.split(marker, 1)[1]
words = []
for line in body.splitlines():
    line = line.strip()
    if not line.startswith("0x"):
        continue
    for tok in line.split()[1:]:
        if tok.startswith("|"):
            break
        if len(tok) == 8 and all(c in "0123456789abcdefABCDEF" for c in tok):
            words.append(tok)
payload = bytes.fromhex("".join(words))
if len(payload) != 24:
    sys.exit("expected 24-byte .text, got %d" % len(payload))
pad = payload[12:]
if pad == bytes(12):
    sys.exit("exec pad is all-zero (invalid Format E)")
if pad[0] == 0:
    sys.exit("exec pad indicator is 0 (all-zero is not Format E)")
