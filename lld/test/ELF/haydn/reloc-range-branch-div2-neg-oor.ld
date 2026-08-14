/* GE96-03 negative one-past: -2056 is below the signed-12 byte window
 * -> long-branch thunk. */
SECTIONS {
  . = 0x10000;
  .text.tgt : { *(.text.tgt) }
  . = 0x10808;
  .text : { *(.text) }
}
