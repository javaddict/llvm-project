; RUN: %python %S/../../../utils/haydn/check_product_ld.py --self-test
; RUN: %python %S/../../../utils/haydn/check_product_ld.py --llvm-src %S/../../../.. --require-sysroot
; RUN: %python %S/../../../utils/haydn/record_haydn_artifact_set.py --self-test
; RUN: %python %S/../../../utils/haydn/record_haydn_artifact_set.py --check-product-ld --llvm-src %S/../../../.. --require-sysroot
; RUN: %python %S/../../../utils/haydn/record_haydn_artifact_set.py --check-product-ld --llvm-src %S/../../../.. --require-sysroot --require-consumer-install
; RUN: %python %S/../../../utils/haydn/record_haydn_artifact_set.py --check-library --llvm-src %S/../../../..
; RUN: %python %S/../../../utils/haydn/check_runtime_artifact_seats.py --self-test
; RUN: %python -c "from pathlib import Path; p=Path(r'%S/../../../lib/Target/Haydn/haydn-rt/PRODUCT-IDENTITY.txt').read_text(); assert 'user-printf.c' in p and 'memset-2.c' in p and 'builtin-bitops-1.c' in p and 'strlen-5.c' in p and 'va-arg-1.c' in p and 'va-arg-2.c' in p and 'CODE_IMAGE_REJECT' in p and 'direct control target is not an exact code record' in p and '4b6677f8bf87' in p and '28700d57' in p and 'edbb813' in p and 'T7-RT LANDED' in p; s=Path(r'%S/../../../lib/Target/Haydn/haydn-rt/SOFTFLOAT-CONTRACT.txt').read_text(); assert 'T-SF10' in s and 'yarpgen' in s and '.bak' in s and '28700d57' in s"
; RUN: %python -c "from pathlib import Path; t=Path(r'%S/../../../lib/Target/Haydn/HaydnISelLowering.cpp').read_text(); assert 'CanLowerReturn' in t and 'RetCC_Haydn' in t and 'HaydnLocIsReservedSoftZero' in t; h=Path(r'%S/../../../lib/Target/Haydn/HaydnCallingConv.h').read_text(); assert 'HaydnCCAssignRejectsType' in h"
; REQUIRES: haydn-registered-target

; Role: harness — T-MC10 / M12 product linker script pin (G-RUNTIME-TOOLCHAIN).
;
; REGRESSION TEST: the BundleSim/sysroot linker script must be versioned in
; the monorepo at haydn-rt/haydn.ld (not only a BSP copy).
; record_haydn_artifact_set.py install_product_ld binds that file as
; lib/haydn.ld + lib/bundlesim.ld and refuses .bak/.broken debris.
; ARTIFACT.product_ld null is fail-closed. --require-consumer-install
; fail-closes on the working-tree install script, the committed
; checkout, and --self-test (refuse .bak/.broken + bind haydn-rt/haydn.ld).
; T7-RT LANDED: BundleSim edbb813.
;
; Bug class: CLAUDE.md advertised haydn-rt/haydn.ld but the directory did not
; exist; install consumed BundleSim bundlesim/bsp/bundlesim.ld with no in-tree
; ident. A memory-map drift in BSP would have no monorepo pin.
;
; Test design: grep HAYDN-LD-IDENT (date + Format E 12-byte parcel) and hash
; the ident-stripped body. When $HAYDN_BIN/../sysroot (or llc-adjacent
; sysroot) has lib/bundlesim.ld, that installed copy must match the in-tree
; body. Do not invent a memory map — the in-tree file is a copy of the live
; product script plus the ident comment.
;
; If the ident disappears or the sysroot copy diverges, this test fails.
