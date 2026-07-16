; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: s64 constant materialization sign-extension optimization (OPT-7).
;
; When hi32 == 0xFFFFFFFF AND lo32 < 0, LOADI64 expand produces hi via
; SRAI32 scr, scr, 31
; after materialising lo (HaydnInstrInfo::expandPostRAPseudo), instead of a
; second MatInt(-1). This is only valid when lo is negative — ashr(lo,31)
; then yields all-ones. Values with hi=-1 but lo>=0 still materialise hi
; independently (SRAI of a non-negative lo is zero, not -1).
;
; G_CONSTANT selects rematerialisable LOADI64; expansion owns the halves.

; Negative s64: hi32 = 0xFFFFFFFF, lo32 = 0xFFFFFFFF (value = -1)
define i64 @neg1() {
; CHECK-LABEL: neg1:
; CHECK: srai32 {{r[0-9]+}}, {{r[0-9]+}}, 31
  ret i64 -1
}

; Negative s64: hi32 = 0xFFFFFFFF, lo32 = 0xFFFFFF00 (value = -256)
define i64 @neg256() {
; CHECK-LABEL: neg256:
; CHECK: srai32 {{r[0-9]+}}, {{r[0-9]+}}, 31
  ret i64 -256
}

; hi32 = 0xFFFFFFFF, lo32 = 0x00000001 (value = 0xFFFFFFFF00000001)
; lo >= 0: OPT-7 does NOT apply (srai32 would give 0, not -1).
define i64 @neg_large() {
; CHECK-LABEL: neg_large:
; CHECK-NOT: srai32
  ret i64 -4294967295
}

; Positive s64: hi32 = 0, lo32 = 0x80000000 (value = 2147483648)
; No SRAI32 needed — hi32 is zero, use ADDI32 r0, 0.
; CHECK that srai32 does NOT appear.
define i64 @pos_lo31_set() {
; CHECK-LABEL: pos_lo31_set:
; CHECK-NOT: srai32
  ret i64 2147483648
}

; General case: hi32 != 0 and hi32 != 0xFFFFFFFF
; Both halves must be materialized independently. No SRAI32 shortcut.
; Value = 0x123456789ABCDEF0
define i64 @general_64bit() {
; CHECK-LABEL: general_64bit:
; CHECK-NOT: srai32
  ret i64 1311768467294899696
}

; Positive s64: hi32 = 0x00000001, lo32 = 0 (value = 0x100000000 = 4294967296)
; hi32 = 1, must be materialized with ADDI32, not SRAI32.
define i64 @pos_hi1() {
; CHECK-LABEL: pos_hi1:
; CHECK-NOT: srai32
  ret i64 4294967296
}
