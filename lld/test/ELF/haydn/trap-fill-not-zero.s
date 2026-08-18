# REQUIRES: haydn
# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf a.s -o a.o
# RUN: ld.lld -T pad.ld a.o -o out
# RUN: llvm-readobj -S out | FileCheck %s --check-prefix=SEC
# RUN: llvm-readobj -x .text out | FileCheck %s --check-prefix=HEX
# RUN: llvm-readobj -x .text out > dump.txt
# RUN: %python check_pad.py dump.txt

# REGRESSION TEST: a trailing linker-script `. = . + 12` uses trap fill.
#
# LLD only writes nopInstrs when OutputSection::getFiller is non-zero.
# trapInstr is ISA-inert zero (indicator != 111) so a 4/8-byte residue
# cannot decode as a phantom bundle (gcc-torture align-3). A trailing
# script pad therefore stays zero. Whole-parcel idle between
# function-sections is the size-pad + relocateAlloc seat
# (e96-function-sections-align-phase.s). Sibling trap-fill-zero-inert.s
# pins the same inert-zero bytes.

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
if pad != bytes(12):
    sys.exit("trailing script pad must be ISA-inert zero fill, got %s" % pad.hex())
if (pad[0] & 0x07) == 0x07:
    sys.exit("pad carries a 111 format indicator (decodable as a bundle head)")
