/* WIDE_BranchSImm12 byte-scale one-past pin: branch@0x10000 → target@0x10804
 * (byte distance +2052 exceeds signed-12 → long-branch thunk). */
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x10804;
  .text.tgt : { *(.text.tgt) }
}
