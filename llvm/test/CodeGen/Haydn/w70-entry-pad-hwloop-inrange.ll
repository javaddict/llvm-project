; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs < %s -o - 2>/dev/null \
; RUN:     | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs < %s -filetype=obj -o %t.o
; RUN: llvm-objdump -d --triple=haydn-unknown-elf %t.o \
; RUN:     | FileCheck %s --check-prefix=DIS
; RUN: llvm-readobj -S --symbols %t.o | FileCheck %s --check-prefix=SEC
; REQUIRES: haydn-registered-target
;
; W70.2r: function-entry alignment is pre-label MC fill, not a post-label
; MachineAlignment extent pad. aligned_loop is first in shared .text at
; offset 0, which already satisfies align 32, so no idle parcels are
; inserted inside the function. HWLoop Off1/Off2 stay function-relative
; from the SET parcel and must remain in-range.
;
;   * aligned_loop (align 32): first symbol, offset 0, no in-function pad.
;     The loop body is never touched: no idle parcel appears between
;     set_hwloop_f2 and the body stores.
;   * grid_loop (default align 4 | 12): follows aligned_loop on the parcel
;     grid with no extra fill (4 divides 12).
;   * SET immediates are H-track (Wave 1/2); this file pins alignment only.

define void @aligned_loop(ptr nocapture writeonly %p, i32 %n) align 32 {
entry:
  br label %body
body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %g = getelementptr i32, ptr %p, i32 %i
  store volatile i32 %i, ptr %g, align 4
  store volatile i32 %i, ptr %g, align 4
  %inc = add i32 %i, 1
  %c = icmp slt i32 %inc, %n
  br i1 %c, label %body, label %exit
exit:
  ret void
}

define void @grid_loop(ptr nocapture writeonly %p, i32 %n) {
entry:
  br label %body
body:
  %i = phi i32 [ 0, %entry ], [ %inc, %body ]
  %g = getelementptr i32, ptr %p, i32 %i
  store volatile i32 %i, ptr %g, align 4
  %inc = add i32 %i, 1
  %c = icmp slt i32 %inc, %n
  br i1 %c, label %body, label %exit
exit:
  ret void
}

; Generic header emits .p2align 5 (align 32) BEFORE the label.
; No post-label idle parcels: prologue is the first size-bearing row.
; The { nop; nop } parcels AFTER set_hwloop_f2 are FixupHwLoops setup-gap
; floors, NOT alignment pads (Wave 6 owns internal MBB/ZOL packets).
; ASM: .p2align 5
; ASM-LABEL: aligned_loop:
; ASM: .cfi_startproc
; ASM-NOT: { nop; nop }
; ASM: xor32
; ASM: set_hwloop_f2
; ASM: .LLhwloop_start0:
; ASM: st32
; ASM: st32
; ASM: .LLhwloop_end0:
;
; grid_loop takes no pad (default align 4 divides the parcel): its
; first parcel is the prologue, not an idle row.
; ASM-LABEL: grid_loop:
; ASM: .cfi_startproc
; ASM-NOT: { nop; nop }
; ASM: xor32
; ASM: set_hwloop_f2

; Object truth: aligned_loop at 0 (already 0 mod 32). SET immediates are
; H-track; this pin is alignment-only — SET must exist, the body store
; follows, and grid_loop is a later shared-.text symbol (not 0x0).
; DIS-LABEL: <aligned_loop>:
; DIS-NEXT: { {{.*}}xor32
; DIS: {{.*}}set_hwloop_f2
; DIS: {{.*}}st32
; DIS-LABEL: <grid_loop>:
; DIS: {{.*}}set_hwloop_f2

; Section: alignment 32. Size is the unpadded parcel stream (H-track
; body length); do not pin an exact byte count here.
; SEC: Name: .text
; SEC: AddressAlignment: 32
; SEC: Name: aligned_loop
; SEC-NEXT: Value: 0x0
