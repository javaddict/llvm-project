/* WIDE_BranchSImm12 GE96-03 byte-window positive in-range pin:
 * branch@0x10000 -> target@0x107F8 (byte distance +2040; signed-12 field is
 * the raw byte displacement, ValueShift=0). */
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x107F8;
  .text.tgt : { *(.text.tgt) }
}
