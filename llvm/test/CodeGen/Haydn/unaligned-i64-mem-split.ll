; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; ABI DataLayout i64:32 allows 4-byte-aligned i64, but ISA ST64/LD64 need 8.
; Unaligned s64 memops must not emit st64/ld64 (BundleSim e_struct ALIGN fault).
; Accept either: (1) MOVE32_DR_L/H + ST32/LD32, or (2) D_SW_L/H / dual LD32 pack.

%struct.P = type { i32, i64, i32 }

@g = external global %struct.P

define void @store_unaligned_i64_field(i64 %v) {
; CHECK-LABEL: store_unaligned_i64_field:
; CHECK: // %bb.0:
; CHECK-NOT: d_sdw_
; Lane word stores of the DR (post-RA may fold extract+ST32 → d_sw_l/h).
; CHECK: {{d_sw_l|s_sw_[a-z_]*|move32_dr}}
; CHECK: jalr{{.*}}lr
entry:
  %p = getelementptr inbounds %struct.P, ptr @g, i32 0, i32 1
  store i64 %v, ptr %p, align 4
  ret void
}

define i64 @load_unaligned_i64_field() {
; CHECK-LABEL: load_unaligned_i64_field:
; CHECK: // %bb.0:
; CHECK-NOT: d_ldw_
; CHECK: s_lw_{{[a-z_]*}}
; CHECK: s_lw_{{[a-z_]*}}
; CHECK: jalr{{.*}}lr
entry:
  %p = getelementptr inbounds %struct.P, ptr @g, i32 0, i32 1
  %v = load i64, ptr %p, align 4
  ret i64 %v
}
