; RUN: %python %S/../../../utils/haydn/check_runtime_artifact_seats.py --self-test
; RUN: %S/../../../utils/haydn/check_runtime_artifact_seats.sh --self-test
; RUN: %python %S/../../../utils/haydn/record_haydn_artifact_set.py --self-test
; RUN: %python %S/../../../utils/haydn/record_haydn_artifact_set.py --check-library --llvm-src %S/../../../..
; RUN: %python %S/../../../utils/haydn/record_haydn_artifact_set.py --check-product-ld --llvm-src %S/../../../.. --require-consumer-install
; RUN: %python %S/../../../utils/haydn/write_ci_verdict.py --self-test
; RUN: %python %S/../../../utils/haydn/classify_lldb_step.py --self-test
; RUN: %python %S/../../../utils/haydn/check_xfail_ledger.py --self-test
; RUN: %python %S/../../../utils/haydn/parse_lit_summary.py --self-test
; RUN: %python %S/../../../utils/haydn/check_product_ld.py --self-test
; RUN: %python %S/../../../utils/haydn/check_runtime_artifact_seats.py --llvm-src %S/../../../..
; RUN: %python -c "from pathlib import Path; p=Path(r'%S/../../../lib/Target/Haydn/haydn-rt/PRODUCT-IDENTITY.txt').read_text(); assert 'user-printf.c' in p and 'memset-2.c' in p and 'strlen-5.c' in p and 'va-arg-1.c' in p and 'va-arg-2.c' in p and 'direct control target is not an exact code record' in p"
; RUN: FileCheck --input-file=%S/../../../../lld/ELF/Arch/Haydn.cpp %s --check-prefix=LLD
; REQUIRES: haydn-registered-target

; Role: harness — same-artifact compiler→sysroot→consumer seats.
;
; Pins library identity (libc+libm / product_library_pin 35/456/491), product_ld bind
; (install_product_ld + .bak/.broken refuse + working-tree consumer install),
; user-printf.c + memset-2.c + strlen-5.c + va-arg-1.c + va-arg-2.c
; classified library seats, CODE_IMAGE_REJECT exact-record residual,
; M10 step-inst residual classes, T-SF9 contract inventory, T-SF10
; hygiene/yarpgen, M13 gcc-torture FP skip + IEEE leftover, M14 EM_HAYDN=259
; fail-closed, whole-parcel idle pad, M16 freeze,
; and CI verdict not-QUALIFY. --require-consumer-install pins the owned
; working-tree install_haydn_sysroot.sh; committed checkout remains residual
; OPEN. Live --require-sysroot is not forced here so a missing sysroot stays
; informational; when ARTIFACT.json is present the library + product_ld
; stamps are required. Does not run BundleSim ctest, gcc-torture, or a libc
; sysroot rebuild. vec_dot16 exactness stays residual.
;
; LLD: CODE_IMAGE_REJECT
; LLD: direct control target is not an exact code record
; LLD: trapInstr = {0x00, 0x00, 0x00, 0x00}
; LLD: nopFiller
