/* HWLoopOff1 ÷4 positive one-past pin: SET@0x10000 → body@0x10100
 * (byte distance +256 exceeds unsigned-6 after ÷4 → relocate fails closed). */
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x10100;
  .text.body : { *(.text.body) }
}
