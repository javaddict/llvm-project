; RUN: FileCheck --input-file=%S/../../../lib/Target/Haydn/haydn-rt/PRODUCT-IDENTITY.txt %s --check-prefix=IDENT
; RUN: FileCheck --input-file=%S/../../../lib/Target/Haydn/haydn-rt/SOFTFLOAT-CONTRACT.txt %s --check-prefix=SF
; RUN: FileCheck --input-file=%S/../../../lib/Target/Haydn/haydn-rt/NATUREDSP-CANARY-PIN.txt %s --check-prefix=LIB
; RUN: FileCheck --input-file=%S/../../../../lld/ELF/Arch/Haydn.cpp %s --check-prefix=LLD
; RUN: FileCheck --input-file=%S/../../../../lldb/source/Plugins/Instruction/Haydn/EmulateInstructionHaydn.cpp %s --check-prefix=STEP
; RUN: FileCheck --input-file=%S/../../../../lldb/source/Plugins/ABI/Haydn/ABISysV_haydn.h %s --check-prefix=ABI
; RUN: FileCheck --input-file=%S/../../../../clang/lib/Driver/ToolChains/Haydn.cpp %s --check-prefix=DRV
; RUN: %python %S/../../../utils/haydn/check_xfail_ledger.py --runtime-pin --llvm-src %S/../../../..
; RUN: %python %S/../../../utils/haydn/parse_lit_summary.py --self-test
; REQUIRES: haydn-registered-target

; Role: harness — T7 owner-path pins for one compiler→sysroot→consumer
; artifact. Classify residuals; do not invent e_machine, IEEE vectors,
; a second library matrix, or a fuzz gate.

; IDENT: ARTIFACT.json
; IDENT: haydn-rt/haydn.ld
; IDENT: beyond_approved=456
; IDENT: EM_HAYDN=259
; IDENT: replacement e_machine
; IDENT: parcel12
; IDENT: same-PC
; IDENT: Do not invent a fuzz gate

; SF: __addsf3
; SF: _Float16
; SF: unavailable
; SF: gcc-torture

; LIB: product_library_pin
; LIB: vec_scale32x32_fast_hifi3
; LIB: 456

; LLD: canonicalFullSlotIdleParcel
; LLD: nopFiller
; LLD: EM_HAYDN=259
; LLD: Kalray KVX
; LLD: Do not invent a replacement

; STEP: kFormatEParcelBytes
; STEP: No member decode
; STEP: EncodedBytes=12

; ABI: kFormatEParcelBytes = 12
; ABI: SamePCResidual
; ABI: ClassifyStepDelta

; DRV: --nmagic
; DRV: -lm
