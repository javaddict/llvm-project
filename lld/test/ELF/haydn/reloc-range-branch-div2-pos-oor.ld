/* GE96-03 positive one-past: +2048 exceeds the signed-12 byte window
 * ([-2048, +2046]) -> long-branch thunk. */
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x10800;
  .text.tgt : { *(.text.tgt) }
}
