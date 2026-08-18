/* WIDE_BranchSImm12 byte-scale negative one-past pin: branch@0x10810 →
 * target@0x10000 (byte distance -2064 exceeds signed-12 → thunk). */
SECTIONS {
  . = 0x10000;
  .text.tgt : { *(.text.tgt) }
  . = 0x10810;
  .text : { *(.text) }
}
