; REQUIRES: asserts
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O0 -verify-machineinstrs < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O0 -verify-machineinstrs -haydn-sms2 < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=LOOP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O0 -debug-pass=Structure -haydn-sms2=0 < %s -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=OFFPIPE
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O0 -haydn-sms2 -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=ONPIPE
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O0 -verify-machineinstrs -haydn-sms2 \
; RUN:     -debug-only=haydn-late-convergence < %s -o /dev/null 2>&1 \
; RUN:     | FileCheck %s --check-prefix=DBG
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnLateConvergence.cpp \
; RUN:     --check-prefix=EXHAUST
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnLateConvergence.cpp \
; RUN:     --check-prefix=NOGROWTH
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/HaydnTargetMachine.cpp \
; RUN:     --check-prefix=FLAG
;
; W68.3R O0/optnone exit-row: the bounded convergence driver sits at
; addPostBBSections under -haydn-sms2 (default off). At -O0 the seat
; still runs for ordinary functions (PostRA pack is legal-encode at
; every opt level). GR2.4 scopes the optnone claim to THIS driver seat
; only: PostMachineScheduler itself is now mandatory for optnone
; (forcePostRAScheduling), but this convergence driver still honors
; skipFunction for optnone. Pins:
;   * flag-off default: no LateConvergence in the O0 pipeline;
;   * flag-on: driver after sanitizer metadata, immediately before
;     closure Finalize; BR remains last inside the mutating iteration;
;   * converges (fixed point, bound not exhausted) under
;     -verify-machineinstrs;
;   * optnone is not entered by the DRIVER (quality skip; the scheduler
;     itself runs — see gr24-optnone-mandatory-sched.ll);
;   * bound exhaustion stays a hard diagnostic (source pin).

define i32 @conv_o0(ptr nocapture readonly %a, i32 %n) {
entry:
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %b, label %e
b:
  %x = load i32, ptr %a, align 4
  %v = add i32 %x, %n
  br label %e
e:
  %r = phi i32 [ 0, %entry ], [ %v, %b ]
  ret i32 %r
}

define i32 @conv_o0_optnone(ptr nocapture readonly %a, i32 %n) noinline optnone {
entry:
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %b, label %e
b:
  %x = load i32, ptr %a, align 4
  %v = add i32 %x, %n
  br label %e
e:
  %r = phi i32 [ 0, %entry ], [ %v, %b ]
  ret i32 %r
}

; Default-off still freezes through ordinary O0 Finalize (jalr epilogue).
; OFF-LABEL: conv_o0:
; OFF: subi32{{.*}}sp, sp,
; OFF: jalr
; OFF-LABEL: conv_o0_optnone:
; OFF: jalr

; LOOP-LABEL: conv_o0:
; LOOP: subi32{{.*}}sp, sp,
; LOOP: jalr
; LOOP-LABEL: conv_o0_optnone:
; LOOP: jalr

; OFFPIPE pins the flag-DISABLED pipeline (G004 trim 2026-08-27: -haydn-sms2
; is product default ON, so the bare default now contains the driver).
; OFFPIPE-NOT: Haydn Late Layout Convergence Loop

; ONPIPE: Machine Sanitizer Binary Metadata
; ONPIPE: Haydn Late Layout Convergence Loop
; ONPIPE-NEXT: Haydn Bundle Finalization

; Ordinary O0 function is entered; optnone is skipFunction'd BY THE DRIVER
; (the postmisched pack itself runs for optnone since GR2.4).
; DBG: HaydnLateConvergence: conv_o0 bound={{[0-9]+}} (cond={{[0-9]+}} hwloop=0)
; DBG: HaydnLateConvergence: closed after {{[0-9]+}} iteration(s) (no upward event)
; DBG-NOT: conv_o0_optnone
; DBG-NOT: exhausted

; EXHAUST: "HaydnLateConvergence: bounded repair loop exhausted " +

; GR2.6 enforced no-growth law: unaccounted per-iteration prefix growth is
; a named fatal (the discarded (void)NoGrowth site is gone). Illegal
; growth has no healthy-compiler producer — the firing path is proven by
; the HaydnLateConvergenceBudgetTest unit red/green plus this source pin.
; NOGROWTH: "HaydnLateConvergence: prefix budget grew beyond the admitted "
; NOGROWTH-NEXT: "closure vocabulary: " +

; FLAG: "haydn-sms2", cl::Hidden,
; FLAG-NEXT: cl::init(haydnLateConvergenceProductDefaultEnabled()),
