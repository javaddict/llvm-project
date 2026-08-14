; RUN: %python %S/../../../utils/haydn/check_xfail_ledger.py --self-test
; RUN: %python %S/../../../utils/haydn/check_xfail_ledger.py --llvm-src %S/../../../..
; REQUIRES: haydn-registered-target

; Role: harness — live CodeGen/MC Haydn XFAIL set must match
; Inputs/XFAIL-OWNER-LEDGER.txt (TOTAL + PATH set; every row owned).
;
; G-TEST-EVIDENCE: unexplained XFAIL is a product failure. When adding or
; removing an XFAIL, update the ledger in the same change. Do not invent
; GE96 wire answers to clear an owned residual.
