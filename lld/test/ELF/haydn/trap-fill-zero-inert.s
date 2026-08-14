# REQUIRES: haydn
# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf a.s -o a.o
# RUN: ld.lld -T pad.ld a.o -o out
# RUN: llvm-readobj -S out | FileCheck %s --check-prefix=SEC
# RUN: llvm-readobj -x .text out | FileCheck %s --check-prefix=HEX
# RUN: llvm-readobj -x .text out > dump.txt
# RUN: %python check_pad.py dump.txt

# REGRESSION TEST: the fixed 4-byte trap filler must be ISA-INERT (zero).
#
# History, both directions:
#
#   * trapInstr = {0,0,0,0} was once flagged because "all-zero is not a
#     Format E bundle (indicator must be 111)" and the frontend of the day
#     choked on zero parcels in .text.  That frontend premise is retired:
#     BundleSim's coverage walk (CB-146) inspects the bytes and accepts
#     zero gaps as not-code.
#
#   * The replacement — seeding trapInstr from the first 4 bytes of the
#     generated idle parcel — planted a 0b111 format indicator inside
#     gaps that CANNOT hold a bundle.  Function alignment sits on the
#     4-byte lattice while the parcel is 12 bytes, so an aligned(N>4)
#     function leaves a dead residue of 4 or 8 (mod 12) bytes.  With an
#     idle-seeded trapInstr the linear-sweep disassembler decoded a
#     phantom bundle straddling the next function's entry, the symbol
#     restart re-decoded the same PCs, and BundleSim rejected the image
#     (CODE_IMAGE_REJECT — gcc-torture align-3, aligned(256)).
#
# Contract pinned here: trapInstr stays zero because indicator != 0b111 is
# the one encoding that says "these bytes are not code".  Whole-parcel
# executable padding is a different mechanism (nopInstrs = the generated
# idle parcel) and is NOT what this file pins.  check_pad.py asserts the
# 12-byte script pad tiles to zero and never carries a 111 indicator.

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
    sys.exit("script pad must be ISA-inert zero fill, got %s" % pad.hex())
if (pad[0] & 0x07) == 0x07:
    sys.exit("pad carries a 111 format indicator (decodable as a bundle head)")
