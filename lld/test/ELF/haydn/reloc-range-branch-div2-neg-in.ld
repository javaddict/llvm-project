/* WIDE_BranchSImm12 byte-scale negative in-range pin: branch@0x10800 →
 * target@0x10000 (byte distance -2048 is the signed-12 lower bound). */
SECTIONS {
  . = 0x10000;
  .text.tgt : { *(.text.tgt) }
  . = 0x10800;
  .text : { *(.text) }
}
