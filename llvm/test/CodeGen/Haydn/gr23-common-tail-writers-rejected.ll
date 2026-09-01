; RUN: not llc -mtriple=haydn-unknown-elf -O2 -enable-machine-outliner=always \
; RUN:     < %s 2>&1 | FileCheck %s --check-prefix=OUTL
; RUN: not llc -mtriple=haydn-unknown-elf -O2 -enable-machine-outliner \
; RUN:     < %s 2>&1 | FileCheck %s --check-prefix=OUTL
; RUN: not llc -mtriple=haydn-unknown-elf -O2 -enable-machine-outliner=optimistic-pgo \
; RUN:     < %s 2>&1 | FileCheck %s --check-prefix=OUTL
; RUN: not llc -mtriple=haydn-unknown-elf -O2 -enable-machine-outliner=conservative-pgo \
; RUN:     < %s 2>&1 | FileCheck %s --check-prefix=OUTL
; RUN: not llc -mtriple=haydn-unknown-elf -O2 -enable-split-machine-functions \
; RUN:     < %s 2>&1 | FileCheck %s --check-prefix=SPLIT
; RUN: not llc -mtriple=haydn-unknown-elf -O2 -split-machine-functions \
; RUN:     < %s 2>&1 | FileCheck %s --check-prefix=SPLIT
; RUN: not llc -mtriple=haydn-unknown-elf -O2 -basic-block-sections=all \
; RUN:     < %s 2>&1 | FileCheck %s --check-prefix=BBSEC

; Disable/no-op spellings stay admitted (fail-closed rejects enables only):
; shared multi-triple command lines that globally pass a disable spelling keep
; working on Haydn.
; RUN: llc -mtriple=haydn-unknown-elf -O2 -enable-machine-outliner=never \
; RUN:     < %s > /dev/null
; RUN: llc -mtriple=haydn-unknown-elf -O2 -enable-split-machine-functions=false \
; RUN:     < %s > /dev/null
; RUN: llc -mtriple=haydn-unknown-elf -O2 -basic-block-sections=none \
; RUN:     < %s > /dev/null
; D1.46: the rejection seat reads the STORED cl::opt value directly (no
; printOptionValue/stdout capture — that fd-swap was process-wide under
; in-process parallel codegen and not portable). This combined-disable pin
; locks the typed-read semantics: explicit enum disable + explicit bool
; zero together classify as no-request.
; RUN: llc -mtriple=haydn-unknown-elf -O2 -enable-machine-outliner=never \
; RUN:     -enable-split-machine-functions=0 < %s > /dev/null

; Default path is unchanged: plain -O2 compiles clean with ordinary asm.
; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s --check-prefix=DEF

; OUTL: LLVM ERROR: Haydn: unsupported forced common-tail writer: machine-outliner
; SPLIT: LLVM ERROR: Haydn: unsupported forced common-tail writer: machine-function-splitter
; BBSEC: LLVM ERROR: Haydn: unsupported forced common-tail writer: basic-block-sections

; DEF-LABEL: gr23_src:
; DEF: jalr{{(\.s[012])?}} r0, lr, 0

; GR2.3 closed invariant: no Haydn pipeline configuration that requests
; MachineOutliner, MachineFunctionSplitter, or BasicBlockSections — via any
; enable spelling (llc hidden static flags, clang -fbasic-block-sections /
; -fsplit-machine-functions / -moutline TM bits, C API) — reaches the common
; executable tail. The request itself rejects at pipeline construction in
; HaydnPassConfig::addPreEmitPass, which TargetPassConfig::addMachinePasses
; invokes strictly BEFORE its writer block (outliner, function/static-data
; splitting, BasicBlockSections) and before addPostBBSections closure /
; addPreEmitPass2 freeze — so no outlining/splitting/reordering MI is ever
; created. The `not` arms need no --crash: the diagnostic uses
; GenCrashDiag=false (exit 1, "LLVM ERROR:" prefix).
;
; Deliberate exclusions (scope boundary is law, contracts/pipeline.md common
; tail): static-data splitting (-split-static-data) is data-section-only and
; cannot change executable layout; -basic-block-address-map alone is
; metadata-only. Either would still need its own GOALS qualification item.

define i32 @gr23_src(i32 %x, i32 %y) {
entry:
  %a = add i32 %x, %y
  ret i32 %a
}
