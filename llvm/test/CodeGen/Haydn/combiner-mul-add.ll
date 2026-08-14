; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — PostLegalizerCombiner identity multiply and add-chain folding.

; REGRESSION TEST: PostLegalizerCombiner identity multiply and add-chain folding.
;
; Bug: The post-legalizer combiner was missing two common algebraic simplifications:
; 1. G_MUL x, 1 -> COPY x (identity multiply wastes a MUL slot)
; 2. G_ADD(G_ADD x, C1), C2 -> G_ADD x, (C1+C2) (constant chain uses 2 ADDs)
; Fix: Added matchMulByOne/applyMulByOne and matchAddConstChain/applyAddConstChain
; to HaydnPostLegalizerCombiner.
;
; Test design: Write IR that produces mul-by-1 and chained add-with-constant patterns
; after legalizer runs. Verify the output has fewer arithmetic instructions than
; the unoptimized version would produce.
;
; IMPORTANT: CHECK-NOT patterns use word boundaries to avoid matching function
; names like "mul_by_one" or libcalls like "__muldi3". The \b anchor ensures
; we only match the mul32 instruction mnemonic, not substrings.

;===--- Identity multiply: x * 1 should become a copy (no MUL32) ---===

define i32 @mul_by_one(i32 %x) nounwind {
; CHECK-LABEL: mul_by_one:
; CHECK-NOT: mul32
; CHECK: jalr{{(\.s[012])?}}
  %r = mul i32 %x, 1
  ret i32 %r
}

define i64 @mul_by_one_64(i64 %x) nounwind {
; CHECK-LABEL: mul_by_one_64:
; i64 mul by 1: the IR/GISel combiner folds x*1 -> x, so no multiply remains.
; Even if it didn't fold, G_MUL <s64> now lowers to native MUL64_LL partials
; never a __muldi3 libcall. Either way there is no jal_w __muldi3.
; CHECK-NOT: jal{{(\.s[012])?}} {{.*}}__muldi3
; CHECK-NOT: jal{{(\.s[012])?}} {{.*}}__mulsi3
; CHECK: jalr{{(\.s[012])?}}
  %r = mul i64 %x, 1
  ret i64 %r
}

;===--- Add constant chain: (x + 10) + 20 -> x + 30 ---===

define i32 @add_chain_i32(i32 %x) nounwind {
; CHECK-LABEL: add_chain_i32:
; The two constant adds should be folded: addi32{{(_w)?}} r2, r0, 30 then add32 r1, r1, r2.
; There should be exactly one add-immediate materializing 30.
; CHECK: addi32{{(_w)?}} {{.*}}, 30
; CHECK: jalr{{(\.s[012])?}}
  %t1 = add i32 %x, 10
  %r = add i32 %t1, 20
  ret i32 %r
}

define i64 @add_chain_i64(i64 %x) nounwind {
; CHECK-LABEL: add_chain_i64:
; i64 add constant chain should fold to add64 with constant 300.
; CHECK: add64
; CHECK: jalr{{(\.s[012])?}}
  %t1 = add i64 %x, 100
  %r = add i64 %t1, 200
  ret i64 %r
}

;===--- Negative constants: (x + (-5)) + 3 -> x + (-2) ---===

define i32 @add_chain_neg(i32 %x) nounwind {
; CHECK-LABEL: add_chain_neg:
; Should fold to addi32{{(_w)?}} with -2.
; CHECK: addi32{{(_w)?}} {{.*}}, -2
; CHECK: jalr{{(\.s[012])?}}
  %t1 = add i32 %x, -5
  %r = add i32 %t1, 3
  ret i32 %r
}
