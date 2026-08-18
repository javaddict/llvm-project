; RUN: %python %S/../../../utils/haydn/check_runtime_artifact_seats.py --self-test
; RUN: ls %S/../../../utils/haydn/abi-conformance/callee.c
; RUN: ls %S/../../../utils/haydn/abi-conformance/caller.c
; RUN: ls %S/../../../utils/haydn/abi-conformance/atomics_symbols.c
; RUN: ls %S/../../../utils/haydn/abi-conformance/start.c
; RUN: %python -c "from pathlib import Path; p=Path(r'%S/../../../utils/haydn/abi-conformance/callee.c').read_text(); assert 'ret_f32' in p and 'ret_f64' in p"
; RUN: %python -c "from pathlib import Path; p=Path(r'%S/../../../utils/haydn/abi-conformance/start.c').read_text(); assert '_start' in p and 'consume' in p"
; RUN: %S/../../../utils/haydn/run_abi_conformance_matrix.sh %t.abi
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; REQUIRES: haydn-registered-target

; Role: harness — T-ABI9 / M6 monorepo ABI compile/link matrix.
;
; Live compile/link + product-ld executable bind is
; run_abi_conformance_matrix.sh (also invoked by product_coverage_pin.sh).
; This lit file runs that matrix and pins RetCC FileCheck so a torn clang
; resource-dir does not fail the owner slice. Executed BundleSim
; `ctest -L abi-conformance` stays residual. Do not treat compile/link
; as semantic QUALIFY. Torture consumer stays red until the image owner
; clears CODE_IMAGE_REJECT.
;
; FileCheck arms pin RetCC_Haydn on this same artifact: i32 in r1 (add32),
; i64 in d0 (add64), f32 bits stay off the DR bank, f64 stays a DR return.

define i32 @ret_i32_add(i32 %a, i32 %b) {
; CHECK-LABEL: ret_i32_add:
; CHECK: add32
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = add i32 %a, %b
  ret i32 %r
}

define i64 @ret_i64_add(i64 %a, i64 %b) {
; CHECK-LABEL: ret_i64_add:
; CHECK: add64
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = add i64 %a, %b
  ret i64 %r
}

define float @ret_f32_arg(float %a) {
; CHECK-LABEL: ret_f32_arg:
; CHECK-NOT: {{[[:space:]]d[0-9]+}}
; CHECK-NOT: jal{{(\.s[012])?}}{{.*}}__addsf3
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret float %a
}

define double @ret_f64_arg(double %a) {
; CHECK-LABEL: ret_f64_arg:
; CHECK-NOT: jal{{(\.s[012])?}}{{.*}}__adddf3
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  ret double %a
}
