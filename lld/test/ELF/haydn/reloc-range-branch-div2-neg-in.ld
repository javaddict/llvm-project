/* GE96-03 negative in-range pin: branch@0x10800 -> target@0x10000
 * (byte distance -2048 = signed-12 minimum). */
SECTIONS {
  . = 0x10000;
  .text.tgt : { *(.text.tgt) }
  . = 0x10800;
  .text : { *(.text) }
}
