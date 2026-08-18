; RUN: %python %S/../../../utils/haydn/classify_lldb_step.py --self-test
; RUN: %python %S/../../../utils/haydn/write_ci_verdict.py --self-test
; RUN: %python %S/../../../utils/haydn/check_product_ld.py --self-test
; RUN: %python %S/../../../utils/haydn/check_product_ld.py --llvm-src %S/../../../.. --require-sysroot
; RUN: %python %S/../../../utils/haydn/record_haydn_artifact_set.py --self-test
; RUN: %python %S/../../../utils/haydn/record_haydn_artifact_set.py --check-product-ld --llvm-src %S/../../../.. --require-sysroot --require-consumer-install
; RUN: %python %S/../../../utils/haydn/record_haydn_artifact_set.py --check-library --llvm-src %S/../../../.. --require-sysroot
; RUN: %python %S/../../../utils/haydn/check_runtime_artifact_seats.py --self-test
; RUN: %python %S/../../../utils/haydn/check_runtime_artifact_seats.py --llvm-src %S/../../../.. --require-sysroot --require-consumer-install
; REQUIRES: haydn-registered-target

; Role: harness — compiler to sysroot to consumer artifact identity.
;
; Pins haydn-rt/haydn.ld as the product linker script, .bak/.broken refuse,
; ARTIFACT.product_ld attach (null is fail-closed), library pin (vec_dot16
; residual), memset-2/strlen-5/va-arg library seats, CODE_IMAGE_REJECT
; exact-record residual, LLDB step parcel12 preferred with same-PC/non-12
; residual, gcc-torture float/double skip classified, and yarpgen frozen-28.
; --require-consumer-install fail-closes on the working-tree install script.
; The committed BundleSim checkout bind stays residual OPEN.
; Compile/link is never semantic QUALIFY. Does not run BundleSim ctest.
