; RUN: %python %S/../../../utils/haydn/check_product_ld.py --self-test
; RUN: %python %S/../../../utils/haydn/check_product_ld.py --llvm-src %S/../../../..
; REQUIRES: haydn-registered-target

; Role: harness — T-MC10 / M12 product linker script pin (G-RUNTIME-TOOLCHAIN).
;
; REGRESSION TEST: the BundleSim/sysroot linker script must be versioned in
; the monorepo at haydn-rt/haydn.ld (not only a BSP copy).
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
