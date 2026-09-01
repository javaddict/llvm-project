; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:   -haydn-enable-hwloops=0 < %s 2>&1 | FileCheck %s --check-prefix=HWOFF
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj < %s \
; RUN:   -o %t.o
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:   -haydn-enable-hwloops=0 -filetype=obj -o %t.def.o < %s
; RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | \
; RUN:   FileCheck %s --check-prefix=OBJ
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC

; 2026-08-22 hwloop product-default flip rebaseline: default is now ON.
; DEFAULT pins ON formation; HWOFF keeps the explicit-OFF residual.

; Role: object — BEGIN/END displacement QUALIFY under the product default
; (ON since 2026-08-22). Immediate hwloop is PC+(uimm<<2); Off1/Off2 must
; be scale-aligned. DEFAULT arms set_hwloop_f2 sel=0 with START/END
; labels; HWOFF is the software residual (no SET). The OBJ dump is the
; load-bearing gate: resolved Off1/Off2 are 12-byte Format E parcel
; multiples (and therefore <<2-legal). Nested outer stays soft (measured
; multi-BB miss); inner may arm sel=0 only. Never free HWLR CSR.

define i32 @sum_arr(ptr %a, i32 %n) {
; DEFAULT-LABEL: sum_arr:
; DEFAULT:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DEFAULT-NOT:   set_hwloop_f2 1,
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       jalr
; HWOFF-LABEL: sum_arr:
; HWOFF-NOT:   set_hwloop
; HWOFF:       jalr
entry:
  br label %for.body

for.body:
  %i = phi i32 [ 0, %entry ], [ %next, %for.body ]
  %s = phi i32 [ 0, %entry ], [ %acc, %for.body ]
  %p = getelementptr inbounds i32, ptr %a, i32 %i
  %v = load i32, ptr %p
  %acc = add i32 %s, %v
  %next = add i32 %i, 1
  %cond = icmp slt i32 %next, %n
  br i1 %cond, label %for.body, label %for.end

for.end:
  %r = phi i32 [ %acc, %for.body ]
  ret i32 %r
}

define i32 @bigimm(ptr %a, i32 %n) {
; DEFAULT-LABEL: bigimm:
; DEFAULT:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DEFAULT-NOT:   set_hwloop_f2 1,
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       jalr
; HWOFF-LABEL: bigimm:
; HWOFF-NOT:   set_hwloop
; HWOFF:       jalr
entry:
  br label %for.body

for.body:
  %i = phi i32 [ 0, %entry ], [ %next, %for.body ]
  %s = phi i32 [ 0, %entry ], [ %acc, %for.body ]
  %p = getelementptr inbounds i32, ptr %a, i32 %i
  %v = load i32, ptr %p
  %mul = mul i32 %v, 74565           ; 0x12345 — hoisted WIDE addi32
  %acc = add i32 %s, %mul
  %next = add i32 %i, 1
  %cond = icmp slt i32 %next, %n
  br i1 %cond, label %for.body, label %for.end

for.end:
  %r = phi i32 [ %acc, %for.body ]
  ret i32 %r
}

; Nested: inner single-BB may arm sel=0; outer stays a software back-edge
; (measured multi-BB miss). Per-instance START/END labels stay distinct.
define i32 @nested_hwloop(ptr noalias %a, i32 %n, i32 %m) {
; DEFAULT-LABEL: nested_hwloop:
; DEFAULT:       set_hwloop_f2 0, .LLhwloop_start{{[0-9]*}}, .LLhwloop_end{{[0-9]*}},
; DEFAULT-NOT:   set_hwloop_f2 1,
; DEFAULT-NOT:   set_hwloop_f2 2,
; DEFAULT-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DEFAULT:       .LLhwloop_start
; DEFAULT:       .LLhwloop_end
; DEFAULT:       bnez
; DEFAULT:       jalr
; HWOFF-LABEL: nested_hwloop:
; HWOFF-NOT:   set_hwloop
; HWOFF:       bnez
; HWOFF:       jalr
entry:
  br label %outer.header

outer.header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %outer.latch ]
  %s = phi i32 [ 0, %entry ], [ %s.inner, %outer.latch ]
  br label %inner.body

inner.body:
  %j = phi i32 [ 0, %outer.header ], [ %j.next, %inner.body ]
  %si = phi i32 [ %s, %outer.header ], [ %si.acc, %inner.body ]
  %idx = add i32 %i, %j
  %p = getelementptr inbounds i32, ptr %a, i32 %idx
  %v = load i32, ptr %p
  %si.acc = add i32 %si, %v
  %j.next = add i32 %j, 1
  %c.j = icmp slt i32 %j.next, %m
  br i1 %c.j, label %inner.body, label %outer.latch

outer.latch:
  %s.inner = phi i32 [ %si.acc, %inner.body ]
  %i.next = add i32 %i, 1
  %c.i = icmp slt i32 %i.next, %n
  br i1 %c.i, label %outer.header, label %for.end

for.end:
  %r = phi i32 [ %s.inner, %outer.latch ]
  ret i32 %r
}

; Matching-artifact object: resolved Off1/Off2 (Format E parcel
; multiples, <<2-legal). DEFAULT asm pins the three SET sites
; (sum_arr / bigimm / nested inner) at sel=0. Retired HWLoopOffset
; stays fail-closed; ordinary BranchSImm16 is allowed.

; OBJ:      set_hwloop_f2
; OBJ-NOT:  csrw
; RELOC-NOT: HWLoopOffset
