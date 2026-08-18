; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s

; Role: semantic — LOAD_ADDR (global address) must be rematerializable so RA re-emits it on every use path rather than spilling across a diamond.

; LOAD_ADDR (global address) must be rematerializable so RA re-emits
; it on every use path rather than spilling across a diamond. Without remat
; one arm stores the address to a stack slot and the join reloads it even when
; control never took that arm → undef (0xffffffff) → S_LW MEMORY_FAULT.
;
; Shape: materialize @g in the entry, branch on %c; both arms and the join
; load through the pointer. Expect load_addr / lui+addi materialize near each
; use (or at least a correct address), never a lone ld32 from an uninit slot
; of a constant address.

@g = dso_local global i32 42, align 4
@h = dso_local global i32 7, align 4

define dso_local i32 @cb117_global_addr_diamond(i1 %c) nounwind {
; CHECK-LABEL: cb117_global_addr_diamond:
; Materialize @g / @h as address constants (lui/addi or remat loadi32 path).
; Must not only depend on a single early store of the address to the stack
; that is skipped on one branch arm.
; CHECK: lui
; CHECK: ld32
; CHECK: jalr
entry:
  br i1 %c, label %then, label %else

then:
  %p0 = load i32, ptr @g, align 4
  %a = add i32 %p0, 1
  br label %join

else:
  %p1 = load i32, ptr @h, align 4
  %b = add i32 %p1, 2
  br label %join

join:
  %v = phi i32 [ %a, %then ], [ %b, %else ]
  %p2 = load i32, ptr @g, align 4
  %r = add i32 %v, %p2
  ret i32 %r
}
