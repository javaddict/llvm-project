/* HWLoopOff1 ÷4 positive in-range pin: SET@0x10000 → body@0x100F0
 * (byte distance +240 = unsigned-6 field 60 after word scale). Off2 to
 * end@0x100FC (body + one Format E 12-byte parcel) is +252 (field 63). */
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x100F0;
  .text.body : { *(.text.body) }
}
