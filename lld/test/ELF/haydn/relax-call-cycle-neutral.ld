/* Nearby .text at 0x10000; .text.far beyond signed-20 CallSImm20 (±512KiB). */
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x200000;
  .text.far : { *(.text.far) }
}
