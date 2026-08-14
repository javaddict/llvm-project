; RUN: %python %S/../../../utils/haydn/write_ci_verdict.py --self-test
; RUN: %python %S/../../../utils/haydn/write_ci_verdict.py --fixture %S/../../../utils/haydn/testdata/ci-verdict-lit-green.log --out %t.pass.json --require-overall PASS --head-sha testdata
; RUN: %python %S/../../../utils/haydn/write_ci_verdict.py --dry-run %t.pass.json
; RUN: %python %S/../../../utils/haydn/write_ci_verdict.py --fixture %S/../../../utils/haydn/testdata/ci-verdict-lit-fail.log --out %t.fail.json --require-overall FAIL --head-sha testdata
; RUN: %python %S/../../../utils/haydn/write_ci_verdict.py --dry-run %t.fail.json
; REQUIRES: haydn-registered-target

; Role: harness — CI verdict artifact from a fake lit log, no product gate.
;
; Green fixture has Expectedly Failed and no Failed line; overall must be PASS.
; Fail fixture has Failed: 1; overall must be FAIL. Schema is checked via
; --dry-run. Does not run CodeGen/MC Haydn lit, ninja, libc, or BundleSim.
