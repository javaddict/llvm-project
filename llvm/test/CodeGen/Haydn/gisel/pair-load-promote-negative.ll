; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=haydn-postlegalizer-combiner -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
;
; Role: semantic — NEGATIVE controls for DR64 pair-load promotion
; (bkfir report §7 ask #3). Every function here must stay scalar.
;
; REGRESSION TEST: the pair-load combine must be fail-closed. These forms
; each violate one closed condition of matchCombinePairLoad
; (HaydnPostLegalizerCombiner.cpp):
;   @nonadjacent      — words 8 bytes apart (not i, i+4): no combine.
;   @intervening_store — aliasing store between the two loads: barrier
;                        rejection (condition 6); widening would read the
;                        post-store word as the low half. Silent wrong code
;                        if this ever combines.
;   @shared_low_word   — the low load feeds a second GPR use: the scalar
;                        word is still needed, so the pack is not exclusive
;                        (condition 3, one-use).
;   @underaligned_2    — D1.126: sub-word MMO align 2; 4-align consecutive
;                        i32 is the product pair path (pair-load-promote.ll
;                        @pair_pack_align4), not a refuse.
; If any of these starts showing a single wide G_LOAD / ld64, the matcher
; has become unsound — fix the condition, do not update the CHECK.

define i64 @nonadjacent(ptr %p) {
; MIR-LABEL: name: nonadjacent
; MIR: G_OR
; MIR-NOT: G_LOAD {{.*}}(s64)
; ASM-LABEL: nonadjacent:
; ASM: or64
; ASM-NOT: ld64
entry:
  %plo = getelementptr i32, ptr %p, i32 2
  %phi = getelementptr i32, ptr %p, i32 4
  %lo = load i32, ptr %plo, align 8
  %hi = load i32, ptr %phi, align 8
  %lo64 = zext i32 %lo to i64
  %hi64 = zext i32 %hi to i64
  %his = shl i64 %hi64, 32
  %d = or i64 %his, %lo64
  ret i64 %d
}

define i64 @intervening_store(ptr %p, ptr %q) {
; MIR-LABEL: name: intervening_store
; MIR: G_STORE
; MIR: G_OR
; MIR-NOT: G_LOAD {{.*}}(s64)
; ASM-LABEL: intervening_store:
; ASM: or64
; ASM-NOT: ld64
entry:
  %plo = getelementptr i32, ptr %p, i32 2
  %phi = getelementptr i32, ptr %p, i32 3
  %lo = load i32, ptr %plo, align 8
  store i32 %lo, ptr %q
  %hi = load i32, ptr %phi, align 8
  %lo64 = zext i32 %lo to i64
  %hi64 = zext i32 %hi to i64
  %his = shl i64 %hi64, 32
  %d = or i64 %his, %lo64
  ret i64 %d
}

define i64 @shared_low_word(ptr %p, ptr %out) {
; MIR-LABEL: name: shared_low_word
; MIR: G_OR
; MIR-NOT: G_LOAD {{.*}}(s64)
; ASM-LABEL: shared_low_word:
; ASM: or64
; ASM-NOT: ld64
entry:
  %plo = getelementptr i32, ptr %p, i32 2
  %phi = getelementptr i32, ptr %p, i32 3
  %lo = load i32, ptr %plo, align 8
  %hi = load i32, ptr %phi, align 8
  %lo64 = zext i32 %lo to i64
  %hi64 = zext i32 %hi to i64
  %his = shl i64 %hi64, 32
  %d = or i64 %his, %lo64
  store i32 %lo, ptr %out
  ret i64 %d
}

; D1.126: sub-word align — still refuse. Product 4-align consecutive i32
; forms a pair (see pair-load-promote.ll @pair_pack_align4).
define i64 @underaligned_2(ptr %p) {
; MIR-LABEL: name: underaligned_2
; MIR: G_OR
; MIR-NOT: G_LOAD {{.*}}(s64)
; ASM-LABEL: underaligned_2:
; ASM: or64
; ASM-NOT: ld64
entry:
  %plo = getelementptr i32, ptr %p, i32 2
  %phi = getelementptr i32, ptr %p, i32 3
  %lo = load i32, ptr %plo, align 2
  %hi = load i32, ptr %phi, align 2
  %lo64 = zext i32 %lo to i64
  %hi64 = zext i32 %hi to i64
  %his = shl i64 %hi64, 32
  %d = or i64 %his, %lo64
  ret i64 %d
}
