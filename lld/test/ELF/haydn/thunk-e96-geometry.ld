/* Place far target beyond CallSImm20 signed-20-bit byte window (±512 KiB). */
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x10000 + 0x100000;
  .text.far : { *(.text.far) }
}
