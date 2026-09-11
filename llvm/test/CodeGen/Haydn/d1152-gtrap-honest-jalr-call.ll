; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs -o - %s \
; RUN:     | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %s | FileCheck %s --check-prefix=ASM
;
; D1.152: G_TRAP / G_DEBUGTRAP / G_UBSANTRAP select the D1.130 returning-call
; role (LOAD_ADDR + one JALR_CALL + csr_haydn), not PseudoCALLIndirect, and
; not a second abort call. ExpandPseudos leftover still maps
; PseudoCALLIndirect → JALR_CALL (HaydnInstrInfo.cpp); ISel must emit the
; honest opcode itself. Peer: AIE1InstrInfo.cpp:688 JAL vs JAL_IND;
; HaydnCallLowering.cpp LOAD_ADDR + JALR_CALL. RISC-V trap→UNIMP is a
; different ISA; Haydn has no golden trap encoding (A.5 abort libcall).
; Dead-end soft RET (jalr r0, lr, 0) is HaydnEnsureTerminators, not a
; second abort.

define void @t_trap() noreturn {
; ISEL-LABEL: name: t_trap
; ISEL: LOAD_ADDR{{.*}}abort
; ISEL: JALR_CALL
; ISEL-SAME: csr_haydn
; ISEL-NOT: JALR_CALL
; ISEL-NOT: PseudoCALLIndirect
; ASM-LABEL: t_trap:
; ASM: lui{{.*}}abort
; ASM: addi32{{.*}}abort
; ASM: jalr{{(\.s[012])?}} lr,
; ASM-NOT: jalr{{(\.s[012])?}} lr,
  call void @llvm.trap()
  unreachable
}

define void @t_debugtrap() noreturn {
; ISEL-LABEL: name: t_debugtrap
; ISEL: LOAD_ADDR{{.*}}abort
; ISEL: JALR_CALL
; ISEL-SAME: csr_haydn
; ISEL-NOT: JALR_CALL
; ISEL-NOT: PseudoCALLIndirect
; ASM-LABEL: t_debugtrap:
; ASM: lui{{.*}}abort
; ASM: addi32{{.*}}abort
; ASM: jalr{{(\.s[012])?}} lr,
; ASM-NOT: jalr{{(\.s[012])?}} lr,
  call void @llvm.debugtrap()
  unreachable
}

define void @t_ubsantrap() noreturn {
; ISEL-LABEL: name: t_ubsantrap
; ISEL: LOAD_ADDR{{.*}}abort
; ISEL: JALR_CALL
; ISEL-SAME: csr_haydn
; ISEL-NOT: JALR_CALL
; ISEL-NOT: PseudoCALLIndirect
; ASM-LABEL: t_ubsantrap:
; ASM: lui{{.*}}abort
; ASM: addi32{{.*}}abort
; ASM: jalr{{(\.s[012])?}} lr,
; ASM-NOT: jalr{{(\.s[012])?}} lr,
  call void @llvm.ubsantrap(i8 1)
  unreachable
}

declare void @llvm.trap()
declare void @llvm.debugtrap()
declare void @llvm.ubsantrap(i8 immarg)
