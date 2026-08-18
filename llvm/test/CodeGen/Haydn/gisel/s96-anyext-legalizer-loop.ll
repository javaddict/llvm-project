; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -stop-after=legalizer -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=LEG
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=legalizer -verify-machineinstrs < %s -o - \
; RUN:   | FileCheck %s --check-prefix=LEG
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -verify-machineinstrs < %s -o - 2>&1 | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -enable-misched=false -enable-post-misched=false \
; RUN:     -verify-machineinstrs < %s -o - 2>&1 | FileCheck %s
;
; Role: MIR — G_ANYEXT s96→s128 must terminate. Pre-fix, unmerge/lshr/store
; of the non-pow2 source re-entered this custom arm and hung. COPY of a
; load/trunc, AND/OR/XOR RMW, SELECT, ADD, and IMPLICIT_DEF rebuild as
; s64 halves (merge) instead. i128/i96 stay out of the ABI; zext results
; are truncated back to i64. The test is that llc finishes.

; LEG-LABEL: name: s96_zext_load
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; LEG-NOT: G_EXTRACT {{.*}}(s96)
; CHECK-LABEL: s96_zext_load:
; CHECK: jalr
define void @s96_zext_load(ptr %p, ptr %q) nounwind {
  %v = load i96, ptr %p, align 4
  %e = zext i96 %v to i128
  %lo = trunc i128 %e to i64
  store i64 %lo, ptr %q, align 8
  ret void
}

; LEG-LABEL: name: s96_and_rmw
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; LEG-NOT: G_EXTRACT {{.*}}(s96)
; CHECK-LABEL: s96_and_rmw:
; CHECK: jalr
define void @s96_and_rmw(ptr %p) nounwind {
  %v = load i96, ptr %p, align 4
  %c = and i96 %v, -4294967296
  store i96 %c, ptr %p, align 4
  ret void
}

; LEG-LABEL: name: s96_or_rmw
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; LEG-NOT: G_EXTRACT {{.*}}(s96)
; CHECK-LABEL: s96_or_rmw:
; CHECK: jalr
define void @s96_or_rmw(ptr %p, ptr %ins_p) nounwind {
  %v = load i96, ptr %p, align 4
  %ins = load i96, ptr %ins_p, align 4
  %c = or i96 %v, %ins
  store i96 %c, ptr %p, align 4
  ret void
}

; LEG-LABEL: name: s96_xor_rmw
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; LEG-NOT: G_EXTRACT {{.*}}(s96)
; CHECK-LABEL: s96_xor_rmw:
; CHECK: jalr
define void @s96_xor_rmw(ptr %p, ptr %ins_p) nounwind {
  %v = load i96, ptr %p, align 4
  %ins = load i96, ptr %ins_p, align 4
  %c = xor i96 %v, %ins
  store i96 %c, ptr %p, align 4
  ret void
}

; LEG-LABEL: name: s96_select
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; LEG-NOT: G_EXTRACT {{.*}}(s96)
; CHECK-LABEL: s96_select:
; CHECK: jalr
define void @s96_select(i32 %c, ptr %pt, ptr %pf, ptr %q) nounwind {
  %cond = icmp ne i32 %c, 0
  %t = load i96, ptr %pt, align 4
  %f = load i96, ptr %pf, align 4
  %s = select i1 %cond, i96 %t, i96 %f
  %e = zext i96 %s to i128
  %lo = trunc i128 %e to i64
  store i64 %lo, ptr %q, align 8
  ret void
}

; LEG-LABEL: name: s96_add
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; LEG-NOT: G_EXTRACT {{.*}}(s96)
; CHECK-LABEL: s96_add:
; CHECK: jalr
define void @s96_add(ptr %p, ptr %q) nounwind {
  %a = load i96, ptr %p, align 4
  %b = load i96, ptr %q, align 4
  %s = add i96 %a, %b
  store i96 %s, ptr %p, align 4
  ret void
}

; LEG-LABEL: name: s96_sub
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; LEG-NOT: G_EXTRACT {{.*}}(s96)
; CHECK-LABEL: s96_sub:
; CHECK: jalr
define void @s96_sub(ptr %p, ptr %q) nounwind {
  %a = load i96, ptr %p, align 4
  %b = load i96, ptr %q, align 4
  %s = sub i96 %a, %b
  store i96 %s, ptr %p, align 4
  ret void
}

; LEG-LABEL: name: s96_undef_anyext
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; LEG-NOT: G_EXTRACT {{.*}}(s96)
; CHECK-LABEL: s96_undef_anyext:
; CHECK: jalr
define void @s96_undef_anyext(ptr %q) nounwind {
  %e = zext i96 undef to i128
  %lo = trunc i128 %e to i64
  store i64 %lo, ptr %q, align 8
  ret void
}

; COPY-of-load: gcc-layout bitfield insert walks the COPY so the load
; arm still fires. Without the walk this used to re-enter ANYEXT.
; LEG-LABEL: name: s96_copy_of_load_insert
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; LEG-NOT: G_EXTRACT {{.*}}(s96)
; CHECK-LABEL: s96_copy_of_load_insert:
; CHECK: jalr
define void @s96_copy_of_load_insert(ptr %p, i32 %v) nounwind {
  %s = load i96, ptr %p, align 4
  %z = zext i32 %v to i96
  %cleared = and i96 %s, -4294967296
  %ins = or i96 %cleared, %z
  store i96 %ins, ptr %p, align 4
  ret void
}

; zext i32→i96 is dest s96 (not s128). Generic widenScalar grows the
; source, so this used to abort. Ext-to-s128 then trunc.
; LEG-LABEL: name: s96_zext_i32
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; LEG-NOT: G_EXTRACT {{.*}}(s96)
; CHECK-LABEL: s96_zext_i32:
; CHECK: jalr
define void @s96_zext_i32(ptr %p, i32 %v) nounwind {
  %z = zext i32 %v to i96
  store i96 %z, ptr %p, align 4
  ret void
}

; Bitfield insert at bit 32: shl of a dest-s96 zext. ANYEXT(shl s96)
; must not G_LSHR the s96 source.
; LEG-LABEL: name: s96_shl_insert
; LEG-NOT: G_ANYEXT {{.*}}(s96)
; LEG-NOT: G_EXTRACT {{.*}}(s96)
; CHECK-LABEL: s96_shl_insert:
; CHECK: jalr
define void @s96_shl_insert(ptr %p, i32 %v) nounwind {
  %s = load i96, ptr %p, align 4
  %z = zext i32 %v to i96
  %sh = shl i96 %z, 32
  %cleared = and i96 %s, -18446744069414584321
  %ins = or i96 %cleared, %sh
  store i96 %ins, ptr %p, align 4
  ret void
}
