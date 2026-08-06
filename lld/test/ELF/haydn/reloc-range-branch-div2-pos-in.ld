/* WIDE_BranchSImm12 ÷2 positive in-range pin: branch@0x10000 → target@0x10FF0
 * (byte distance +4080 = signed-12 field 2040 after halfword scale). */
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x10FF0;
  .text.tgt : { *(.text.tgt) }
}
