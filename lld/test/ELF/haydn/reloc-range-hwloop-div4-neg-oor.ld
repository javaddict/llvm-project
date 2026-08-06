/* HWLoopOff1/Off2 negative pin: body before SET is unsigned-out-of-range after ÷4
 * (lld must fail closed — no parallel signed-range table). */
SECTIONS {
  . = 0x10000;
  .text.body : { *(.text.body) }
  . = 0x10100;
  .text : { *(.text) }
}
