; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnMachineFunctionInfo.cpp --check-prefix=CLONE
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnMachineFunctionInfo.h --check-prefix=DECL
;
; W68.2R clone lifecycle pin: clone() copies MFI via cloneInfo (AIE
; identity peer), then overlays pointer-keyed and per-function state so
; outlined/cloned MFs do not inherit S1/S2 invocation count or source
; DDG keys. Fail mode: dest inherits PostRASchedInvocations and skips
; or mis-fires first-S2 reopen (counter == 2).

; CLONE: Copy->AltDescs.clear();
; CLONE: Copy->InterBlockRegistry.reset();
; CLONE: Copy->PostRASchedInvocations = 0;

; DECL: clone() zeros this so outlined/cloned MFs do not inherit the source
; DECL: unsigned PostRASchedInvocations = 0;
; DECL: std::shared_ptr<HaydnInterBlockEdgesRegistry> InterBlockRegistry;
