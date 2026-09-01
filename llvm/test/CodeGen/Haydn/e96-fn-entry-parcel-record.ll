; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -filetype=obj %s -o %t.o0
; RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o0 | \
; RUN:   FileCheck %s --check-prefix=DIS
; RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o0 > %t.dis0
; RUN: %python -c "import re,sys; bad=[l.strip() for l in open(sys.argv[1]) if (m:=re.match(r'^\s*([0-9a-f]+):', l)) and int(m.group(1),16)%%12]; print('\n'.join(bad)); sys.exit(1 if bad else 0)" %t.dis0
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -filetype=obj %s -o %t.o2
; RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o2 > %t.dis2
; RUN: %python -c "import re,sys; bad=[l.strip() for l in open(sys.argv[1]) if (m:=re.match(r'^\s*([0-9a-f]+):', l)) and int(m.group(1),16)%%12]; print('\n'.join(bad)); sys.exit(1 if bad else 0)" %t.dis2
; REQUIRES: haydn-registered-target
;
; Compiler function entries and direct control targets must be exact
; Format E records (PC ≡ 0 mod 12). Every emitted instruction is one
; 12-byte parcel.

define void @f() noinline {
  ret void
}

define void @g() noinline {
  call void @f()
  ret void
}

; DIS-LABEL: <f>:
; DIS:         0:
; DIS-LABEL: <g>:
; DIS:         jal
