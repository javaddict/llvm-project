; RUN: %python %S/classify_lldb_step.py --self-test
; RUN: %python %S/tdsp13_declared_vs_tested_pin.py --self-test
; RUN: %python %S/check_runtime_artifact_seats.py --self-test
; RUN: %python %S/check_runtime_artifact_seats.py --llvm-src %S/../../..
; RUN: %python %S/parse_lit_summary.py --self-test
; RUN: %python %S/check_xfail_ledger.py --self-test
; RUN: %python %S/check_xfail_ledger.py --runtime-pin --llvm-src %S/../../..
; RUN: %python %S/check_product_ld.py --self-test
; RUN: %python %S/check_product_ld.py --llvm-src %S/../../.. --require-sysroot
; RUN: %python %S/record_haydn_artifact_set.py --self-test
; RUN: %python %S/record_haydn_artifact_set.py --check-product-ld --llvm-src %S/../../.. --require-sysroot --require-consumer-install
; RUN: %python %S/record_haydn_artifact_set.py --check-library --llvm-src %S/../../..
; RUN: %python %S/write_ci_verdict.py --self-test
; RUN: %S/run_abi_conformance_matrix.sh %t.abi
; RUN: %python -c "from pathlib import Path; p=Path(r'%S/../../lib/Target/Haydn/haydn-rt/PRODUCT-IDENTITY.txt').read_text(); assert 'user-printf.c' in p and 'memset-2.c' in p and 'builtin-bitops-1.c' in p and 'strlen-5.c' in p and 'CODE_IMAGE_REJECT' in p and '38bd4059' in p and '28700d57' in p and 'T-ABI4' in p and 'T-ABI6' in p and 'T-ABI11' in p and 'T-ABI12' in p and 'G-ECOSYSTEM-CONSUMERS' in p and 'G-LIBRARY-COVERAGE' in p and 'G-DEBUG-OBSERVABILITY' in p and 'G-TEST-EVIDENCE' in p; s=Path(r'%S/../../lib/Target/Haydn/haydn-rt/SOFTFLOAT-CONTRACT.txt').read_text(); assert 'T-SF10' in s and 'yarpgen' in s and '.bak' in s and '28700d57' in s; w=Path(r'%S/../../lib/Target/Haydn/MCTargetDesc/HaydnELFObjectWriter.cpp').read_text(); assert 'static_assert(ELF::EM_HAYDN == 259' in w and 'KVX' in w and 'do not invent' in w.lower(); t=Path(r'%S/../../lib/Target/Haydn/TargetInfo/HaydnTargetInfo.cpp').read_text(); assert 'EM_HAYDN=259' in t and 'KVX' in t"
; REQUIRES: haydn-registered-target

; Role: harness — T-RT same-artifact residual seats.
;
; Pins the monorepo classifier / hygiene / ledger / parser / CI-verdict
; helpers owned by llvm/utils/haydn, including product-ld bind
; (haydn-rt/haydn.ld + .bak/.broken refuse + ARTIFACT.product_ld),
; user-printf.c + memset-2.c + builtin-bitops-1.c + strlen-5.c
; classified library seats, torture CODE_IMAGE_REJECT residual, 38bd4059
; post-wave rebind (28700d57 refused), T-SF10 hygiene + frozen yarpgen,
; T-SF4 FP-in-gate compiler-rt symbols, and M5 T-ABI4/6/11/12 residual
; classification (no i128 / ISR-R0 / 'd' invent). Does not run BundleSim
; ctest or gcc-torture. Compile-only is never semantic QUALIFY.
