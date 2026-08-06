/* WIDE_BranchSImm12 ÷2 negative one-past pin: branch@0x11010 → target@0x10000
 * (byte distance -4112 exceeds signed-12 after ÷2 → long-branch thunk). */
SECTIONS {
  . = 0x10000;
  .text.tgt : { *(.text.tgt) }
  . = 0x11010;
  .text : { *(.text) }
}
