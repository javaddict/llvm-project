; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — struct argument passing and return values.

; Test struct argument passing and return values.
; Small structs ({i32,i32}, {i32,i32,i32}) are passed in GPR registers.
; Large structs are passed via sret (pointer to caller-allocated memory).

;Return {i32, i32} struct (fits in 2 GPRs)

%struct.pair = type { i32, i32 }

define %struct.pair @return_pair(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: return_pair:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = insertvalue %struct.pair undef, i32 %a, 0
  %r2 = insertvalue %struct.pair %r, i32 %b, 1
  ret %struct.pair %r2
}

;Accept {i32, i32} struct argument
define i32 @accept_pair(%struct.pair %s) nounwind {
; CHECK-LABEL: accept_pair:
; CHECK: add32
  %f0 = extractvalue %struct.pair %s, 0
  %f1 = extractvalue %struct.pair %s, 1
  %sum = add i32 %f0, %f1
  ret i32 %sum
}

;Pass pair to function
define i32 @call_with_pair(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: call_with_pair:
; CHECK: jal{{(\.s[012])?}}
  %p = insertvalue %struct.pair undef, i32 %a, 0
  %p2 = insertvalue %struct.pair %p, i32 %b, 1
  %r = call i32 @accept_pair(%struct.pair %p2)
  ret i32 %r
}

;Return {i32, i32, i32} struct (fits in 3 GPRs)
define { i32, i32, i32 } @return_triple(i32 %a, i32 %b, i32 %c) nounwind {
; CHECK-LABEL: return_triple:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = insertvalue { i32, i32, i32 } undef, i32 %a, 0
  %r2 = insertvalue { i32, i32, i32 } %r, i32 %b, 1
  %r3 = insertvalue { i32, i32, i32 } %r2, i32 %c, 2
  ret { i32, i32, i32 } %r3
}

;Accept {i32, i32, i32} struct argument
define i32 @accept_triple({ i32, i32, i32 } %s) nounwind {
; CHECK-LABEL: accept_triple:
; CHECK: add32
  %f0 = extractvalue { i32, i32, i32 } %s, 0
  %f1 = extractvalue { i32, i32, i32 } %s, 1
  %f2 = extractvalue { i32, i32, i32 } %s, 2
  %s1 = add i32 %f0, %f1
  %sum = add i32 %s1, %f2
  ret i32 %sum
}

;Return struct with i64 field (uses DR64 register)
define { i64, i32 } @return_i64_struct(i64 %a, i32 %b) nounwind {
; CHECK-LABEL: return_i64_struct:
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = insertvalue { i64, i32 } undef, i64 %a, 0
  %r2 = insertvalue { i64, i32 } %r, i32 %b, 1
  ret { i64, i32 } %r2
}

;Accept struct with i64 field
define i64 @accept_i64_struct({ i64, i32 } %s) nounwind {
; CHECK-LABEL: accept_i64_struct:
  %v = extractvalue { i64, i32 } %s, 0
  ret i64 %v
}

;Struct with 4 i32 fields (fits exactly in 4 GPRs)
define { i32, i32, i32, i32 } @return_four(i32 %a, i32 %b, i32 %c, i32 %d) nounwind {
; CHECK-LABEL: return_four:
  %r = insertvalue { i32, i32, i32, i32 } undef, i32 %a, 0
  %r2 = insertvalue { i32, i32, i32, i32 } %r, i32 %b, 1
  %r3 = insertvalue { i32, i32, i32, i32 } %r2, i32 %c, 2
  %r4 = insertvalue { i32, i32, i32, i32 } %r3, i32 %d, 3
  ret { i32, i32, i32, i32 } %r4
}

;Accept 4-field struct
define i32 @accept_four({ i32, i32, i32, i32 } %s) nounwind {
; CHECK-LABEL: accept_four:
; CHECK: add32
  %f0 = extractvalue { i32, i32, i32, i32 } %s, 0
  %f1 = extractvalue { i32, i32, i32, i32 } %s, 1
  %f2 = extractvalue { i32, i32, i32, i32 } %s, 2
  %f3 = extractvalue { i32, i32, i32, i32 } %s, 3
  %s1 = add i32 %f0, %f1
  %s2 = add i32 %s1, %f2
  %sum = add i32 %s2, %f3
  ret i32 %sum
}

;Return pair of i64 (uses 2 DR64 registers)
define { i64, i64 } @return_i64_pair(i64 %a, i64 %b) nounwind {
; CHECK-LABEL: return_i64_pair:
  %r = insertvalue { i64, i64 } undef, i64 %a, 0
  %r2 = insertvalue { i64, i64 } %r, i64 %b, 1
  ret { i64, i64 } %r2
}
