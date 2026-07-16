; RUN: opt -passes=verify -disable-output < %s
;
; REGRESSION TEST: intrinsic-gap closure — IR arity must match DB semantics.
;
; Bug (found while verifying the intrinsic audit closure): the
; bulk-add of 158 intrinsics declared 45 of them with the wrong arity:
; 29 accumulate families were declared `haydn_binary_intrinsic` (2 params)
; but their DB `behavior` is read-modify-write on rtd
; (e.g. FMULA16_HS00: `rtd[63:32] = SAT(rtd[63:32] +...)`), so the
; intrinsic must be TERNARY (acc, a, b) — dropping the accumulator is the
; silent-miscompute class.
; 16 zero-accumulate (Z-prefix) families were declared `haydn_ternary_intrinsic`
; but their DB `behavior` zeros the accumulator
; (e.g. F2MULZAA32RS_HHLL: `rtd = 0 + r_temp1 + r_temp0`), so the
; intrinsic must be BINARY (a, b) — no acc input.
; The audit's "442/442 = 100% arity match" was a buggy-vs-buggy match (both the
; intrinsic AND its builtin carried the same wrong arity); this test pins the
; corrected contract.
;
; Test design: each `declare` uses the CORRECT arity for its intrinsic and a
; real callsite invokes it. `opt -passes=verify` runs the IR verifier and exits
; 0 only if every callsite matches its intrinsic's tablegen signature. If any
; intrinsic's arity regresses in IntrinsicsHaydn.td, `opt` fails with one of:
; "Intrinsic has incorrect argument type!" (params too many)
; "Callsite was not defined with variable arguments!" (params too few)
; and this RUN line fails. This deliberately uses `opt` (not `llc`) so the test
; runs the verifier WITHOUT entering CodeGen — the gap intrinsics have no
; selector patterns yet (selector follow-up), so `llc` would crash in
; InstructionSelect regardless of arity correctness. This test pins the IR
; level arity contract only, which is the regression that was fixed.
;
; References:
; ~/haydn-plans/reviews/intrinsic-builtin-audit-.md
; ~/haydn-plans/decisions/-d208-arity-reconciliation.md
; sibling: test/CodeGen/Haydn/fmuls32s-lh-ternary-arity-regression.ll
; spec: Database/haydn_instruction_db.json (accumulator families read rtd
; Z-prefix families zero the accumulator)

; TERNARY (acc-read) families: (acc, a, b). DB: rtd = rtd +...
declare i64 @llvm.haydn.fmula16.hs00(i64, i64, i64)    ; FMULA16_HS00: rtd[63:32]+=...
declare i64 @llvm.haydn.fmula16.ls33(i64, i64, i64)    ; FMULA16_LS33: rtd[31:00]+=...
declare i64 @llvm.haydn.f2mulaa32r.hhll(i64, i64, i64) ; F2MULAA32R_HHLL: rtd+=round(hh+ll)
declare i64 @llvm.haydn.f2mulss32r.hllh(i64, i64, i64) ; F2MULSS32R_HLLH: rtd-=cross
declare i64 @llvm.haydn.mulaa32.hhll(i64, i64, i64)    ; MULAA32_HHLL: rtd += hh + ll
declare i64 @llvm.haydn.mulas32.hllh(i64, i64, i64)    ; MULAS32_HLLH: rtd += cross
declare i64 @llvm.haydn.mul16aq(i64, i64, i64)         ; MUL16AQ: rtd += 4-lane quad mac

; BINARY (zero-accumulate / Z-prefix) families: (a, b). DB: rtd = 0 +...
declare i64 @llvm.haydn.f2mulzaa32rs.hhll(i64, i64)    ; F2MULZAA32RS: rtd = 0 + r(hh)+r(ll)
declare i64 @llvm.haydn.f2mulzss32rs.hllh(i64, i64)    ; F2MULZSS32RS_HLLH: rtd = 0 - cross
declare i64 @llvm.haydn.fmulzaa32s.hhll(i64, i64)      ; FMULZAA32S: rtd = SAT(0 + hh + ll)
declare i64 @llvm.haydn.fmulzss32s.hllh(i64, i64)      ; FMULZSS32S_HLLH: rtd = SAT(0 - cross)

; Pure multiply (already-correct binary anchors from)
declare i64 @llvm.haydn.mul64.hh(i64, i64)             ; MUL64_HH: rtd = hh*hh
declare i32 @llvm.haydn.brev32(i32, i32)               ; BREV32: rt = bitrev(bitrev(rs1)+rs2)

; If any of these intrinsics reverts to binary, `opt` fails the verifier
; ("Callsite was not defined with variable arguments!") and this RUN fails.
define i64 @anchor_ternary_acc_read(i64 %acc, i64 %a, i64 %b) {
  %t0 = call i64 @llvm.haydn.fmula16.hs00(i64 %acc, i64 %a, i64 %b)
  %t1 = call i64 @llvm.haydn.fmula16.ls33(i64 %t0, i64 %a, i64 %b)
  %t2 = call i64 @llvm.haydn.f2mulaa32r.hhll(i64 %t1, i64 %a, i64 %b)
  %t3 = call i64 @llvm.haydn.f2mulss32r.hllh(i64 %t2, i64 %a, i64 %b)
  %t4 = call i64 @llvm.haydn.mulaa32.hhll(i64 %t3, i64 %a, i64 %b)
  %t5 = call i64 @llvm.haydn.mulas32.hllh(i64 %t4, i64 %a, i64 %b)
  %t6 = call i64 @llvm.haydn.mul16aq(i64 %t5, i64 %a, i64 %b)
  ret i64 %t6
}

; If any of these Z-prefix intrinsics reverts to ternary, `opt` fails the
; verifier ("Intrinsic has incorrect argument type!") and this RUN fails.
define i64 @anchor_binary_zero_init(i64 %a, i64 %b) {
  %z0 = call i64 @llvm.haydn.f2mulzaa32rs.hhll(i64 %a, i64 %b)
  %z1 = call i64 @llvm.haydn.f2mulzss32rs.hllh(i64 %a, i64 %b)
  %z2 = call i64 @llvm.haydn.fmulzaa32s.hhll(i64 %a, i64 %b)
  %z3 = call i64 @llvm.haydn.fmulzss32s.hllh(i64 %a, i64 %b)
  %m  = call i64 @llvm.haydn.mul64.hh(i64 %a, i64 %b)
  %s0 = add i64 %z0, %z1
  %s1 = add i64 %s0, %z2
  %s2 = add i64 %s1, %z3
  %s3 = add i64 %s2, %m
  ret i64 %s3
}

define i32 @anchor_brev32(i32 %a, i32 %b) {
  %r = call i32 @llvm.haydn.brev32(i32 %a, i32 %b)
  ret i32 %r
}
