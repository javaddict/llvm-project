; RUN: %python %S/../../../utils/haydn/classify_lldb_step.py --self-test
; RUN: %python %S/../../../utils/haydn/check_runtime_artifact_seats.py --self-test
; RUN: %S/../../../utils/haydn/check_runtime_artifact_seats.sh --self-test
; RUN: %python %S/../../../utils/haydn/check_xfail_ledger.py --self-test
; RUN: %python %S/../../../utils/haydn/parse_lit_summary.py --self-test
; RUN: %python %S/../../../utils/haydn/check_product_ld.py --self-test
; RUN: %python %S/../../../utils/haydn/record_haydn_artifact_set.py --self-test
; RUN: %python %S/../../../utils/haydn/write_ci_verdict.py --self-test
; REQUIRES: haydn-registered-target

; Role: harness — same-artifact residual seats (alias of product-*).
;
; Pins the monorepo classifier / hygiene / ledger / product-ld / CI-verdict
; helpers owned by llvm/utils/haydn. Decode/consumer identity is required;
; compile-only is never semantic QUALIFY. Does not run BundleSim ctest,
; gcc-torture, or a libc sysroot rebuild.
