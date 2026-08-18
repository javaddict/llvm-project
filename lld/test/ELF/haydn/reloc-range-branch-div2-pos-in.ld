/* WIDE_BranchSImm12 byte-scale in-range pin: branch@0x10000 → target@0x107F8
 * (byte distance +2040 fits signed-12, Align=2; ValueShift=0). */
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x107F8;
  .text.tgt : { *(.text.tgt) }
}
