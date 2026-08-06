/* WIDE_BranchSImm12 ÷2 negative in-range pin: branch@0x11000 → target@0x10000
 * (byte distance -4096 = signed-12 field -2048 after halfword scale). */
SECTIONS {
  . = 0x10000;
  .text.tgt : { *(.text.tgt) }
  . = 0x11000;
  .text : { *(.text) }
}
