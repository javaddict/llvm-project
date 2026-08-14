; RUN: llc -mtriple=haydn-unknown-unknown-elf -O2 -filetype=obj %s -o /dev/null
; REQUIRES: haydn-registered-target
; Role: object — golden-placement law reaches the packing solver.
;
; Reduced from llvm-libc bf16mul (DyadicFloat). The post-RA scheduler used to
; co-schedule D_SW_H_WITH_IMM with ST8 (S_SB_WITH_IMM): slot-legal, but both
; stores seat only LOADSTORE0 at e0 in every Format E row, so serialization
; had no (entry, unit) assignment and died fail-closed:
;   "Haydn MC: Format E one-parcel placement failed for composite with 2 real
;    child(ren) [D_SW_H_WITH_IMM_S2...; ST8_S0...]"
; With golden placement folded into the solver's row frontier
; (refineMaskByGoldenPlacement), the two stores serialize into separate
; cycles and this compiles. The contract of this test is object emission
; succeeding at -O2.


%"struct.__llvm_libc_22_1_8_::fputil::DyadicFloat" = type { %"struct.__llvm_libc_22_1_8_::Sign", i32, %"struct.__llvm_libc_22_1_8_::BigInt" }
%"struct.__llvm_libc_22_1_8_::Sign" = type { i8 }
%"struct.__llvm_libc_22_1_8_::BigInt" = type { %"struct.__llvm_libc_22_1_8_::cpp::array" }
%"struct.__llvm_libc_22_1_8_::cpp::array" = type { [2 x i64] }

; Function Attrs: mustprogress nounwind
define void @bf16mul() #0 {
entry:
  ret void
}

; Function Attrs: inlinehint mustprogress nounwind
define void @_ZN19__llvm_libc_22_1_8_6fputil7generic3mulINS0_8BFloat16EdEENS_3cpp9enable_ifIXaaaasr3cppE19is_floating_point_vIT_Esr3cppE19is_floating_point_vIT0_ElestS6_stS7_ES6_E4typeES7_S7_(double noundef %y) #1 {
entry:
  %xd = alloca %"struct.__llvm_libc_22_1_8_::fputil::DyadicFloat", align 4
  %yd = alloca %"struct.__llvm_libc_22_1_8_::fputil::DyadicFloat", align 4
  %0 = getelementptr inbounds nuw i8, ptr %xd, i32 8
  %1 = getelementptr inbounds nuw i8, ptr %xd, i32 16
  %cmp3.i.i = icmp eq i64 4503599627370496, 0
  br i1 %cmp3.i.i, label %_ZN19__llvm_libc_22_1_8_6fputil11DyadicFloatILj128EEC2IdTnNS_3cpp9enable_ifIXsr3cppE19is_floating_point_vIT_EEiE4typeELi0EEES6_.exit, label %_ZN19__llvm_libc_22_1_8_6BigIntILj128ELb0EyEC2IyvEET_.exit.thread.i

_ZN19__llvm_libc_22_1_8_6BigIntILj128ELb0EyEC2IyvEET_.exit.thread.i: ; preds = %entry
  br label %_ZN19__llvm_libc_22_1_8_6fputil11DyadicFloatILj128EEC2IdTnNS_3cpp9enable_ifIXsr3cppE19is_floating_point_vIT_EEiE4typeELi0EEES6_.exit

_ZN19__llvm_libc_22_1_8_6fputil11DyadicFloatILj128EEC2IdTnNS_3cpp9enable_ifIXsr3cppE19is_floating_point_vIT_EEiE4typeELi0EEES6_.exit: ; preds = %_ZN19__llvm_libc_22_1_8_6BigIntILj128ELb0EyEC2IyvEET_.exit.thread.i, %entry
  %storemerge.i = phi i64 [ 4503599627370496, %_ZN19__llvm_libc_22_1_8_6BigIntILj128ELb0EyEC2IyvEET_.exit.thread.i ], [ 0, %entry ]
  store i64 0, ptr %0, align 4
  store i64 %storemerge.i, ptr %1, align 4
  %2 = getelementptr inbounds nuw i8, ptr %yd, i32 1
  store i8 -1, ptr %2, align 1
  %3 = getelementptr inbounds nuw i8, ptr %yd, i32 2
  store i8 -1, ptr %3, align 2
  %4 = getelementptr inbounds nuw i8, ptr %yd, i32 3
  store i8 -1, ptr %4, align 1
  %5 = getelementptr inbounds nuw i8, ptr %yd, i32 4
  %cmp.i11.i.i119 = icmp eq i32 0, 0
  %6 = add nsw i32 0, -1075
  %7 = select i1 %cmp.i11.i.i119, i32 -1074, i32 %6
  store i32 %7, ptr %5, align 4
  ret void
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #2

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i32(ptr noalias writeonly captures(none), ptr noalias readonly captures(none), i32, i1 immarg) #3

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #2

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.ctlz.i64(i64, i1 immarg) #4

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: write)
declare void @llvm.assume(i1 noundef) #5

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare { i64, i1 } @llvm.uadd.with.overflow.i64(i64, i64) #6

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare { i64, i1 } @llvm.usub.with.overflow.i64(i64, i64) #6

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare double @llvm.fabs.f64(double) #6

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr writeonly captures(none), i8, i64, i1 immarg) #7

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.fshl.i64(i64, i64, i64) #6

attributes #0 = { mustprogress nounwind "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+agu,+hwloop,-bit-reversed,-circular-buffer,-simd" }
attributes #1 = { inlinehint mustprogress nounwind "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+agu,+hwloop,-bit-reversed,-circular-buffer,-simd" }
attributes #2 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #3 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }
attributes #4 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #5 = { nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: write) }
attributes #6 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #7 = { nocallback nofree nounwind willreturn memory(argmem: write) }
