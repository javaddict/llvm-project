/* WIDE_BranchSImm12 ÷2 positive one-past pin: branch@0x10000 → target@0x11000
 * (byte distance +4096 exceeds signed-12 after ÷2 → long-branch thunk). */
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x11000;
  .text.tgt : { *(.text.tgt) }
}
