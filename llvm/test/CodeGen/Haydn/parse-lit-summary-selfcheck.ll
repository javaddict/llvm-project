; RUN: %python %S/../../../utils/haydn/parse_lit_summary.py --self-test
; REQUIRES: haydn-registered-target

; Role: harness — aggregate gate parser must not treat Expectedly Failed as Failed.
;
; Pins llvm/utils/haydn/parse_lit_summary.py false-negative regression samples.
; Full product / yolo gates must use this helper (or equivalent label-exact
; parsing of "Failed:" / "Unexpectedly Passed:") when setting haydn_lit_ok.
