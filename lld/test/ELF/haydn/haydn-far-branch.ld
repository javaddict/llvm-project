/* Place .text.far far enough that BEQ imm12 (÷2) is out of range (±4 KiB). */
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x20000;
  .text.far : { *(.text.far) }
}
