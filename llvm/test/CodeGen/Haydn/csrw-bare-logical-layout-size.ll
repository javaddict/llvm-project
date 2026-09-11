; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 -O2 < %s 2>&1 | FileCheck %s

; Role: semantic — the late layout byte model must charge a product parcel for
; BARE matcher-visible isPseudo catalog logicals (CSRW here), so the Haydn
; Late Layout Convergence identity bake never surfaces unaccounted growth.

; REGRESSION TEST (fleet-4 BundleSim red, kernel cxfir16x16_hifi3):
; getInstSizeInBytes fell through to `MI.isPseudo() -> 0 bytes` for bare
; logical CSRW (HaydnInst<0> isPseudo=1/isCodeGenOnly=0 shell; product encode
; is the generated CSRW_*_I8 member). Inside the convergence driver, S2's
; repack left the two WUR_AE_CBEGIN0/CEND0 boundary CSRWs as bare logicals
; (0 bytes each in every window snapshot), then HaydnLatencyStalls's tail
; applyFinalDirectCompatibleMembers — deliberately Change-silent — baked
; them to CSRW_E2_E0_ALU0_I8 (12 bytes each). The D1.41 accounting then
; fataled: "prefix budget grew beyond the admitted closure vocabulary:
; bb.0->bb.9 grew 24 bytes; admitted vocabulary covers 0".
;
; Fix: HaydnInstrInfo::getInstSizeInBytes charges one product Format E
; parcel for the closed class of matcher-visible isPseudo catalog logicals
; (CSRR/CSRW/ZERO_GPR/SFR-flag 64-bit compares/moves/ALU64 unary block) —
; the same law NOP/WFI already had. A bare shell and its baked member now
; measure identically at every snapshot seat.
;
; CHECK: csrw {{44|0x2c}}
; CHECK: csrw {{45|0x2d}}
; (compile success is the pin: the pre-fix build aborts in the backend
;  before any output; both CBR boundary writes must survive.)

; ModuleID = '/ssd2/mhyang/haydn-plans/hifi_naturedsp_reports/src/library/fir/firblk/cxfir16x16_hifi3.c'
source_filename = "/ssd2/mhyang/haydn-plans/hifi_naturedsp_reports/src/library/fir/firblk/cxfir16x16_hifi3.c"
target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-unknown-elf"

%union.tag_complex_fract16 = type { i32 }

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define dso_local void @cxfir16x16_process(ptr noundef captures(none) %_cxfir, ptr noalias noundef %y, ptr noalias noundef readonly captures(none) %x, i32 noundef %N) local_unnamed_addr #2 {
entry:
  %cmp = icmp slt i32 %N, 1
  br i1 %cmp, label %cleanup, label %if.end

if.end:                                           ; preds = %entry
  %M1 = getelementptr inbounds nuw i8, ptr %_cxfir, i32 4
  %0 = load i32, ptr %M1, align 4, !tbaa !16
  %wrIx2 = getelementptr inbounds nuw i8, ptr %_cxfir, i32 20
  %1 = load i32, ptr %wrIx2, align 4, !tbaa !20
  %delayLine = getelementptr inbounds nuw i8, ptr %_cxfir, i32 12
  %2 = load ptr, ptr %delayLine, align 4, !tbaa !18
  %add.ptr.idx = shl nsw i32 %1, 2
  %add.ptr = getelementptr inbounds i8, ptr %2, i32 %add.ptr.idx
  %3 = ptrtoint ptr %2 to i32
  tail call void @llvm.haydn.setcbr.begin(i32 0, i32 %3)
  %4 = load ptr, ptr %delayLine, align 4, !tbaa !18
  %delayLen = getelementptr inbounds nuw i8, ptr %_cxfir, i32 16
  %5 = load i32, ptr %delayLen, align 4, !tbaa !19
  %add.ptr7.idx = shl nsw i32 %5, 2
  %add.ptr7 = getelementptr inbounds i8, ptr %4, i32 %add.ptr7.idx
  %6 = ptrtoint ptr %add.ptr7 to i32
  %cmp8 = icmp eq ptr %4, null
  %sub = add i32 %6, -1
  %cond = select i1 %cmp8, i32 0, i32 %sub
  tail call void @llvm.haydn.setcbr.end(i32 0, i32 %cond)
  tail call void @llvm.haydn.flar(i32 1)
  %shr = lshr i32 %N, 2
  %cmp11304.not = icmp eq i32 %shr, 0
  br i1 %cmp11304.not, label %for.end137, label %for.body.lr.ph

for.body.lr.ph:                                   ; preds = %if.end
  %coef = getelementptr inbounds nuw i8, ptr %_cxfir, i32 8
  %shr39 = ashr i32 %0, 1
  %cmp40.not282 = icmp slt i32 %shr39, 0
  %add76 = shl i32 %0, 2
  br label %for.body

for.body:                                         ; preds = %for.body.lr.ph, %for.end
  %D_wr.0308 = phi ptr [ %add.ptr, %for.body.lr.ph ], [ %11, %for.end ]
  %X.0307 = phi ptr [ %x, %for.body.lr.ph ], [ %add.ptr17, %for.end ]
  %Y.0306 = phi ptr [ %y, %for.body.lr.ph ], [ %add.ptr132, %for.end ]
  %n.0305 = phi i32 [ 0, %for.body.lr.ph ], [ %inc136, %for.end ]
  %7 = load ptr, ptr %coef, align 4, !tbaa !17
  %8 = load i64, ptr %X.0307, align 8, !tbaa !8
  %add.ptr13 = getelementptr inbounds nuw i8, ptr %X.0307, i32 8
  %9 = load i64, ptr %add.ptr13, align 8, !tbaa !8
  %add.ptr17 = getelementptr inbounds nuw i8, ptr %X.0307, i32 16
  %10 = tail call ptr @llvm.haydn.sdw.cb.reg(i64 %8, ptr %D_wr.0308, i32 0, i32 8)
  %11 = tail call ptr @llvm.haydn.sdw.cb.reg(i64 %9, ptr %10, i32 0, i32 8)
  %add.ptr27 = getelementptr inbounds nuw i8, ptr %11, i32 4
  %add.ptr29 = getelementptr inbounds nuw i8, ptr %11, i32 12
  tail call void @llvm.haydn.pldwwua(i32 0, ptr nonnull %add.ptr27)
  tail call void @llvm.haydn.pldwwua(i32 0, ptr nonnull %add.ptr29)
  br i1 %cmp40.not282, label %for.end, label %do.body43.preheader

do.body43.preheader:                              ; preds = %for.body
  %add.ptr28 = getelementptr inbounds nuw i8, ptr %11, i32 8
  br label %do.body43

do.body43:                                        ; preds = %do.body43.preheader, %do.body43
  %C.0296 = phi ptr [ %add.ptr80, %do.body43 ], [ %7, %do.body43.preheader ]
  %D_rd0.0295 = phi ptr [ %13, %do.body43 ], [ %11, %do.body43.preheader ]
  %D_rd1.0294 = phi ptr [ %16, %do.body43 ], [ %add.ptr27, %do.body43.preheader ]
  %D_rd2.0293 = phi ptr [ %20, %do.body43 ], [ %add.ptr28, %do.body43.preheader ]
  %D_rd3.0292 = phi ptr [ %23, %do.body43 ], [ %add.ptr29, %do.body43.preheader ]
  %q0r.0291 = phi i64 [ %30, %do.body43 ], [ 0, %do.body43.preheader ]
  %q1r.0290 = phi i64 [ %31, %do.body43 ], [ 0, %do.body43.preheader ]
  %q2r.0289 = phi i64 [ %33, %do.body43 ], [ 0, %do.body43.preheader ]
  %q3r.0288 = phi i64 [ %34, %do.body43 ], [ 0, %do.body43.preheader ]
  %q0i.0287 = phi i64 [ %35, %do.body43 ], [ 0, %do.body43.preheader ]
  %q1i.0286 = phi i64 [ %36, %do.body43 ], [ 0, %do.body43.preheader ]
  %q2i.0285 = phi i64 [ %37, %do.body43 ], [ 0, %do.body43.preheader ]
  %q3i.0284 = phi i64 [ %38, %do.body43 ], [ 0, %do.body43.preheader ]
  %m.0283 = phi i32 [ %inc, %do.body43 ], [ 0, %do.body43.preheader ]
  %12 = tail call { i64, ptr } @llvm.haydn.ldw.cb.reg(ptr %D_rd0.0295, i32 0, i32 8)
  %13 = extractvalue { i64, ptr } %12, 1
  %14 = extractvalue { i64, ptr } %12, 0
  %15 = tail call { i64, ptr } @llvm.haydn.lqhwua.cb.post(ptr readonly %D_rd1.0294, i32 0, i32 0), !noalias !21
  %16 = extractvalue { i64, ptr } %15, 1
  %17 = extractvalue { i64, ptr } %15, 0
  %18 = bitcast i64 %17 to <4 x i16>
  %19 = tail call { i64, ptr } @llvm.haydn.ldw.cb.reg(ptr %D_rd2.0293, i32 0, i32 8)
  %20 = extractvalue { i64, ptr } %19, 1
  %21 = extractvalue { i64, ptr } %19, 0
  %22 = tail call { i64, ptr } @llvm.haydn.lqhwua.cb.post(ptr readonly %D_rd3.0292, i32 0, i32 0), !noalias !24
  %23 = extractvalue { i64, ptr } %22, 1
  %24 = extractvalue { i64, ptr } %22, 0
  %25 = bitcast i64 %24 to <4 x i16>
  %26 = getelementptr i8, ptr %C.0296, i32 %add76
  %add.ptr78 = getelementptr i8, ptr %26, i32 16
  %27 = load <4 x i16>, ptr %add.ptr78, align 8, !tbaa !8
  %28 = load <4 x i16>, ptr %C.0296, align 8, !tbaa !8
  %add.ptr80 = getelementptr inbounds nuw i8, ptr %C.0296, i32 8
  %29 = bitcast i64 %14 to <4 x i16>
  %30 = tail call i64 @llvm.haydn.fmulaa16.hs.11.00(i64 %q0r.0291, <4 x i16> %29, <4 x i16> %28)
  %31 = tail call i64 @llvm.haydn.fmulaa16.hs.11.00(i64 %q1r.0290, <4 x i16> %18, <4 x i16> %28)
  %32 = bitcast i64 %21 to <4 x i16>
  %33 = tail call i64 @llvm.haydn.fmulaa16.hs.11.00(i64 %q2r.0289, <4 x i16> %32, <4 x i16> %28)
  %34 = tail call i64 @llvm.haydn.fmulaa16.hs.11.00(i64 %q3r.0288, <4 x i16> %25, <4 x i16> %28)
  %35 = tail call i64 @llvm.haydn.fmulaa16.hs.11.00(i64 %q0i.0287, <4 x i16> %29, <4 x i16> %27)
  %36 = tail call i64 @llvm.haydn.fmulaa16.hs.11.00(i64 %q1i.0286, <4 x i16> %18, <4 x i16> %27)
  %37 = tail call i64 @llvm.haydn.fmulaa16.hs.11.00(i64 %q2i.0285, <4 x i16> %32, <4 x i16> %27)
  %38 = tail call i64 @llvm.haydn.fmulaa16.hs.11.00(i64 %q3i.0284, <4 x i16> %25, <4 x i16> %27)
  %inc = add nuw nsw i32 %m.0283, 1
  %exitcond.not = icmp eq i32 %m.0283, %shr39
  br i1 %exitcond.not, label %for.end.loopexit, label %do.body43, !llvm.loop !27

for.end.loopexit:                                 ; preds = %do.body43
  %39 = ashr i64 %30, 33
  %extract.t = trunc nsw i64 %39 to i32
  %40 = ashr i64 %35, 33
  %extract.t320 = trunc nsw i64 %40 to i32
  %41 = ashr i64 %31, 33
  %extract.t321 = trunc nsw i64 %41 to i32
  %42 = ashr i64 %36, 33
  %extract.t322 = trunc nsw i64 %42 to i32
  %43 = lshr i32 %extract.t, 16
  %44 = lshr i32 %extract.t321, 16
  %45 = lshr i32 %extract.t322, 16
  %46 = and i32 %extract.t320, -65536
  %47 = or disjoint i32 %43, %46
  %48 = ashr i64 %33, 33
  %extract.t323 = trunc nsw i64 %48 to i32
  %49 = ashr i64 %37, 33
  %extract.t324 = trunc nsw i64 %49 to i32
  %50 = ashr i64 %34, 33
  %extract.t325 = trunc nsw i64 %50 to i32
  %51 = ashr i64 %38, 33
  %extract.t326 = trunc nsw i64 %51 to i32
  %52 = lshr i32 %extract.t323, 16
  %53 = lshr i32 %extract.t325, 16
  %54 = lshr i32 %extract.t326, 16
  %55 = and i32 %extract.t324, -65536
  %56 = or disjoint i32 %52, %55
  br label %for.end

for.end:                                          ; preds = %for.end.loopexit, %for.body
  %q3i.0.lcssa.off0 = phi i32 [ 0, %for.body ], [ %54, %for.end.loopexit ]
  %q1i.0.lcssa.off0 = phi i32 [ 0, %for.body ], [ %45, %for.end.loopexit ]
  %q3r.0.lcssa.off0 = phi i32 [ 0, %for.body ], [ %53, %for.end.loopexit ]
  %q1r.0.lcssa.off0 = phi i32 [ 0, %for.body ], [ %44, %for.end.loopexit ]
  %or25.i = phi i32 [ 0, %for.body ], [ %47, %for.end.loopexit ]
  %or25.i274 = phi i32 [ 0, %for.body ], [ %56, %for.end.loopexit ]
  %or.i = zext i32 %or25.i to i64
  %conv14.i = zext nneg i32 %q1r.0.lcssa.off0 to i64
  %shl15.i = shl nuw nsw i64 %conv14.i, 32
  %or16.i = or disjoint i64 %shl15.i, %or.i
  %conv17.i = zext nneg i32 %q1i.0.lcssa.off0 to i64
  %shl18.i = shl nuw i64 %conv17.i, 48
  %or19.i = or disjoint i64 %or16.i, %shl18.i
  tail call void @llvm.haydn.d.sqhwua.post(i64 %or19.i, ptr %Y.0306, i32 1, i32 8, i32 0)
  %add.ptr118 = getelementptr inbounds nuw i8, ptr %Y.0306, i32 8
  %or.i275 = zext i32 %or25.i274 to i64
  %conv14.i276 = zext nneg i32 %q3r.0.lcssa.off0 to i64
  %shl15.i277 = shl nuw nsw i64 %conv14.i276, 32
  %or16.i278 = or disjoint i64 %shl15.i277, %or.i275
  %conv17.i279 = zext nneg i32 %q3i.0.lcssa.off0 to i64
  %shl18.i280 = shl nuw i64 %conv17.i279, 48
  %or19.i281 = or disjoint i64 %or16.i278, %shl18.i280
  tail call void @llvm.haydn.d.sqhwua.post(i64 %or19.i281, ptr nonnull %add.ptr118, i32 1, i32 8, i32 0)
  %add.ptr132 = getelementptr inbounds nuw i8, ptr %Y.0306, i32 16
  %inc136 = add nuw nsw i32 %n.0305, 1
  %exitcond319.not = icmp eq i32 %inc136, %shr
  br i1 %exitcond319.not, label %for.end137, label %for.body, !llvm.loop !28

for.end137:                                       ; preds = %for.end, %if.end
  %Y.0.lcssa = phi ptr [ %y, %if.end ], [ %add.ptr132, %for.end ]
  %D_wr.0.lcssa = phi ptr [ %add.ptr, %if.end ], [ %11, %for.end ]
  tail call void @llvm.haydn.wbarwua(i32 1, ptr %Y.0.lcssa, i32 0)
  %57 = load ptr, ptr %delayLine, align 4, !tbaa !18
  %sub.ptr.lhs.cast = ptrtoint ptr %D_wr.0.lcssa to i32
  %sub.ptr.rhs.cast = ptrtoint ptr %57 to i32
  %sub.ptr.sub = sub i32 %sub.ptr.lhs.cast, %sub.ptr.rhs.cast
  %shr140 = ashr i32 %sub.ptr.sub, 2
  store i32 %shr140, ptr %wrIx2, align 4, !tbaa !20
  br label %cleanup

cleanup:                                          ; preds = %entry, %for.end137
  ret void
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(write)
declare void @llvm.haydn.setcbr.begin(i32 immarg, i32) #3

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(write)
declare void @llvm.haydn.setcbr.end(i32 immarg, i32) #3

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: write)
declare ptr @llvm.haydn.sdw.cb.reg(i64, ptr captures(none), i32 immarg, i32) #4

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: read)
declare { i64, ptr } @llvm.haydn.ldw.cb.reg(ptr captures(none), i32 immarg, i32) #5

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.haydn.flar(i32 immarg) #6

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.haydn.pldwwua(i32 immarg, ptr) #6

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: read)
declare { i64, ptr } @llvm.haydn.lqhwua.cb.post(ptr captures(none), i32 immarg, i32 immarg) #5

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.haydn.fmulaa16.hs.11.00(i64, <4 x i16>, <4 x i16>) #7

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(write)
declare void @llvm.haydn.d.sqhwua.post(i64, ptr, i32 immarg, i32, i32 immarg) #3

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(write)
declare void @llvm.haydn.wbarwua(i32 immarg, ptr, i32 immarg) #3

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.smax.i32(i32, i32) #8

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i32(ptr writeonly captures(none), i8, i32, i1 immarg) #9

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="haydn" "target-features"="+agu,+bit-reversed,+circular-buffer,+hwloop,+simd" }
attributes #1 = { nofree norecurse nosync nounwind memory(write, argmem: readwrite, inaccessiblemem: none, target_mem0: none, target_mem1: none) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="haydn" "target-features"="+agu,+bit-reversed,+circular-buffer,+hwloop,+simd" }
attributes #2 = { nofree norecurse nosync nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="haydn" "target-features"="+agu,+bit-reversed,+circular-buffer,+hwloop,+simd" }
attributes #3 = { mustprogress nocallback nofree nosync nounwind willreturn memory(write) }
attributes #4 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: write) }
attributes #5 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: read) }
attributes #6 = { mustprogress nocallback nofree nosync nounwind willreturn }
attributes #7 = { mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #8 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #9 = { nocallback nofree nounwind willreturn memory(argmem: write) }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}
!llvm.errno.tbaa = !{!2}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 22.1.8 (https://github.com/llvm/llvm-project.git 0defe7dfa73b418343b761e416ab00316d7d0778)"}
!2 = !{!3, !3, i64 0}
!3 = !{!"int", !4, i64 0}
!4 = !{!"omnipotent char", !5, i64 0}
!5 = !{!"Simple C/C++ TBAA"}
!6 = !{!7, !7, i64 0}
!7 = !{!"short", !4, i64 0}
!8 = !{!4, !4, i64 0}
!9 = distinct !{!9, !10}
!10 = !{!"llvm.loop.mustprogress"}
!11 = distinct !{!11, !10}
!12 = !{!13, !3, i64 0}
!13 = !{!"tag_cxfir16x16_t", !3, i64 0, !3, i64 4, !14, i64 8, !14, i64 12, !3, i64 16, !3, i64 20}
!14 = !{!"p1 short", !15, i64 0}
!15 = !{!"any pointer", !4, i64 0}
!16 = !{!13, !3, i64 4}
!17 = !{!13, !14, i64 8}
!18 = !{!13, !14, i64 12}
!19 = !{!13, !3, i64 16}
!20 = !{!13, !3, i64 20}
!21 = !{!22}
!22 = distinct !{!22, !23, !"haydn_ae_cb_ld_tw: %agg.result"}
!23 = distinct !{!23, !"haydn_ae_cb_ld_tw"}
!24 = !{!25}
!25 = distinct !{!25, !26, !"haydn_ae_cb_ld_tw: %agg.result"}
!26 = distinct !{!26, !"haydn_ae_cb_ld_tw"}
!27 = distinct !{!27, !10}
!28 = distinct !{!28, !10}