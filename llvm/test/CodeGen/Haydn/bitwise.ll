; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — comprehensive bitwise operations (AND, OR, XOR, NOT).

; Test comprehensive bitwise operations (AND, OR, XOR, NOT)

;s32 bitwise AND

define i32 @and_i32(i32 %a, i32 %b) {
; CHECK-LABEL: and_i32:
; CHECK-DAG: {{and32|andi32}}
  %r = and i32 %a, %b
  ret i32 %r
}

;s32 bitwise OR
define i32 @or_i32(i32 %a, i32 %b) {
; CHECK-LABEL: or_i32:
; CHECK-DAG: {{or32|ori32}}
  %r = or i32 %a, %b
  ret i32 %r
}

;s32 bitwise XOR
define i32 @xor_i32(i32 %a, i32 %b) {
; CHECK-LABEL: xor_i32:
; CHECK-DAG: {{xor32|xori32}}
  %r = xor i32 %a, %b
  ret i32 %r
}

;s32 bitwise NOT (via XOR with -1)
define i32 @not_i32(i32 %a) {
; CHECK-LABEL: not_i32:
; CHECK-DAG: {{xor32|xori32}}
  %r = xor i32 %a, -1
  ret i32 %r
}

;AND with immediate (bit mask)
define i32 @and_imm(i32 %a) {
; CHECK-LABEL: and_imm:
; CHECK-DAG: addi32
; CHECK-DAG: {{and32|andi32}}
  %r = and i32 %a, 255
  ret i32 %r
}

;OR with immediate
define i32 @or_imm(i32 %a) {
; CHECK-LABEL: or_imm:
; CHECK-DAG: addi32
; CHECK-DAG: {{or32|ori32}}
  %r = or i32 %a, 16
  ret i32 %r
}

;XOR with immediate
define i32 @xor_imm(i32 %a) {
; CHECK-LABEL: xor_imm:
; CHECK-DAG: addi32
; CHECK-DAG: {{xor32|xori32}}
  %r = xor i32 %a, 42
  ret i32 %r
}

;Bit clear (AND with complement)
define i32 @bit_clear(i32 %a, i32 %mask) {
; CHECK-LABEL: bit_clear:
; CHECK-DAG: {{xor32|xori32}}
; CHECK-DAG: {{and32|andi32}}
  %inv = xor i32 %mask, -1
  %r = and i32 %a, %inv
  ret i32 %r
}

;Bit set (OR with mask)
define i32 @bit_set(i32 %a, i32 %mask) {
; CHECK-LABEL: bit_set:
; CHECK-DAG: {{or32|ori32}}
  %r = or i32 %a, %mask
  ret i32 %r
}

;Bit toggle (XOR with mask)
define i32 @bit_toggle(i32 %a, i32 %mask) {
; CHECK-LABEL: bit_toggle:
; CHECK-DAG: {{xor32|xori32}}
  %r = xor i32 %a, %mask
  ret i32 %r
}

;Extract bit field (mask and shift)
define i32 @extract_bits(i32 %a) {
; CHECK-LABEL: extract_bits:
; Extract bits 8-15: (a >> 8) & 0xFF
; CHECK-DAG: {{srl32|srli32}}
; CHECK-DAG: {{and32|andi32}}
  %shifted = lshr i32 %a, 8
  %masked = and i32 %shifted, 255
  ret i32 %masked
}

;Insert bit field
define i32 @insert_bits(i32 %a, i32 %val) {
; CHECK-LABEL: insert_bits:
; Insert val into bits 8-15: (a & ~0xFF00) | ((val << 8) & 0xFF00)
; CHECK-DAG: {{and32|andi32}}
; CHECK-DAG: {{sll32|slli32}}
; CHECK-DAG: {{or32|ori32}}
  %mask = xor i32 -1, 65280
  %cleared = and i32 %a, %mask
  %shifted = shl i32 %val, 8
  %masked_val = and i32 %shifted, 65280
  %r = or i32 %cleared, %masked_val
  ret i32 %r
}

;Leading zero count (simplified)
define i32 @ctlz_simple(i32 %x) {
; CHECK-LABEL: ctlz_simple:
; Check if zero
  %cmp = icmp eq i32 %x, 0
  br i1 %cmp, label %ret_32, label %calc
ret_32:
  ret i32 32
calc:
  ; Simple implementation - just return a value for testing
  %notx = xor i32 %x, -1
  ret i32 %notx
}
; CHECK-DAG: {{xor32|xori32}}

;Sign bit extraction
define i32 @extract_sign_bit(i32 %x) {
; CHECK-LABEL: extract_sign_bit:
; Extract sign bit (bit 31): (x >> 31) & 1
; CHECK-DAG: {{sra32|srai32}}
; CHECK-DAG: {{and32|andi32}}
  %shifted = ashr i32 %x, 31
  %masked = and i32 %shifted, 1
  ret i32 %masked
}

;Multiple bitwise ops chain
define i32 @bitwise_chain(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: bitwise_chain:
; ((a | b) & c) ^ a
; CHECK-DAG: {{or32|ori32}}
; CHECK-DAG: {{and32|andi32}}
; CHECK-DAG: {{xor32|xori32}}
  %t1 = or i32 %a, %b
  %t2 = and i32 %t1, %c
  %r = xor i32 %t2, %a
  ret i32 %r
}

;De Morgan's law test
define i32 @demorgan(i32 %a, i32 %b) {
; CHECK-LABEL: demorgan:
; ~(a & b) == ~a | ~b
; CHECK-DAG: {{and32|andi32}}
; CHECK-DAG: {{xor32|xori32}}
entry:
  %and = and i32 %a, %b
  %not_and = xor i32 %and, -1
  %not_a = xor i32 %a, -1
  %not_b = xor i32 %b, -1
  %or = or i32 %not_a, %not_b
  %r = add i32 %not_and, %or
  ret i32 %r
}
; The ~x value-nots select NOT32 (GISel Pat for xor x,-1; see bool-ops.ll).
; The old xor32 alternation here was only ever satisfied by the epilogue r0
; re-zero xor, which F24 removed from leaf no-call functions.
; CHECK-DAG: not32
; CHECK-DAG: {{or32|ori32}}
; CHECK-DAG: {{add32|addi32}}
