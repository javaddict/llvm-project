; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=postmisched -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -haydn-postra-ready-subset-auction=false \
; RUN:     -stop-after=postmisched < %s | FileCheck %s

; REGRESSION TEST: post-RA ready-subset auction must terminate in bounded
; time on a wide post-call region (>=10 independently Available SUs).
;
; Bug: llc -O2 -stop-after=postmisched on this exact shape (reduced from
; BundleSim yarpgen seed1 tf_3_foo; all 24 yarpgen seeds hit the same shape)
; spun for minutes at 100% CPU. NOT an unbounded loop: the auction
; (HaydnBundleMaterialize.h auctionReadySubsetCycle) is bounded (~1800
; considerOrder calls per tryCandidate), but every opcodesFormOneLegalCycle
; probe linearly scanned the 3686-row generated FormatEMembers table with
; case-insensitive compares (unitMaskForLogical, unitForMemberSymbol) plus a
; 683-entry strcmp scan (findAltSpan) — O(members x 3686 x strlen) per
; probe, x2 zones per tryCandidate, xN candidate comparisons per pick.
; The stall backtraces landed in strlen/compare_insensitive inside those
; scans (exactTryAddWithCover -> tryApplyAlt ->
; opcodesHaveFormatEUnitCover/memberSymbolsHaveInjectiveUnits).
;
; Fix: one mechanism — memoize the pure lookups over the immutable
; generated tables (process-wide StringMaps built once via call_once;
; binary search on the already-sorted FormatEAltSpans table) in
; HaydnFormatERecords.h, and scoreReadySubsetAuction uses the
; haydnDefaultMCFormats() singleton instead of a per-call local
; HaydnMCFormats. No placement/legality semantics change: same tables, same
; answers, O(1) probes. Auction OFF is the second RUN line as the control
; (that path was always fast); the first RUN line is the contract — with
; the memoized lookups both complete in well under a second.
;
; Test design: the sdiv (-> __divsi3 call) creates the wide post-call
; region where LOAD_ADDR/LD results, MOVT/SEQ/ANDI chains, and 4
; independent ST8/ST16s are simultaneously Available — every pick ranks the
; full auction against every other candidate. If the linear scans return,
; this test times out in lit (the failure mode is wall-clock, so the CHECK
; body only pins that the block was scheduled and bundles formed).

@var_186 = external dso_local local_unnamed_addr global i16, align 2
@var_130 = external dso_local local_unnamed_addr global i16, align 2
@var_136 = external dso_local local_unnamed_addr global i16, align 2
@var_142 = external dso_local local_unnamed_addr global i16, align 2
@var_94  = external dso_local local_unnamed_addr global i16, align 2
@var_106 = external dso_local local_unnamed_addr constant i16, align 2
@var_66  = external dso_local local_unnamed_addr global i16, align 2
@var_76  = external dso_local local_unnamed_addr constant i16, align 2
@var_14  = external dso_local local_unnamed_addr global i16, align 2
@var_22  = external dso_local local_unnamed_addr constant i8, align 1
@var_194 = external dso_local local_unnamed_addr global i8, align 1
@var_126 = external dso_local local_unnamed_addr global i16, align 2
@var_70  = external dso_local local_unnamed_addr global i16, align 2
@var_182 = external dso_local local_unnamed_addr global i16, align 2
@var_218 = external dso_local local_unnamed_addr global i8, align 1
@var_220 = external dso_local local_unnamed_addr global i16, align 2
@var_224 = external dso_local local_unnamed_addr global i16, align 2
@var_226 = external dso_local local_unnamed_addr global i16, align 2

define dso_local void @postra_auction_wide_postcall_region() local_unnamed_addr #0 {
entry:
  %0 = load i16, ptr @var_186, align 2
  %conv = zext i16 %0 to i32
  %1 = load i16, ptr @var_130, align 2
  %conv1 = sext i16 %1 to i32
  %sub = sub nsw i32 %conv, %conv1
  %2 = load i16, ptr @var_136, align 2
  %conv2 = sext i16 %2 to i32
  %mul = shl nsw i32 %conv2, 1
  %mul3 = mul nsw i32 %mul, %sub
  %tobool.not = icmp eq i32 %mul3, 0
  br i1 %tobool.not, label %if.end, label %if.then

if.then:
  %3 = load i16, ptr @var_142, align 2
  %conv14 = sext i16 %3 to i32
  %4 = load i16, ptr @var_94, align 2
  %conv15 = zext i16 %4 to i32
  %5 = load i16, ptr @var_106, align 2
  %conv16 = zext i16 %5 to i32
  %add17 = add nuw nsw i32 %conv16, %conv15
  %sext = shl i32 %add17, 24
  %conv19 = ashr exact i32 %sext, 24
  %6 = load i16, ptr @var_66, align 2
  %conv21 = sext i16 %6 to i32
  %7 = load i16, ptr @var_76, align 2
  %conv22 = zext i16 %7 to i32
  %8 = load i16, ptr @var_14, align 2
  %conv24 = sext i16 %8 to i32
  %mul25 = mul nsw i32 %conv24, 9267
  %9 = load i8, ptr @var_22, align 1
  %conv27 = sext i8 %9 to i32
  %add23 = add nsw i32 %conv21, 72
  %add26 = add nsw i32 %add23, %conv22
  %chain1 = add nsw i32 %add26, %conv19
  %add20 = add nsw i32 %chain1, %mul25
  %sub31 = sub nsw i32 %add20, %conv27
  %10 = load i8, ptr @var_194, align 1
  %conv33 = sext i8 %10 to i32
  %11 = add nsw i32 %sub31, %conv33
  %sub34 = sub nsw i32 %conv14, %11
  %12 = load i16, ptr @var_126, align 2
  %tobool36.not = icmp eq i16 %12, 0
  %cond41 = select i1 %tobool36.not, i32 33260, i32 %conv22
  %13 = load i16, ptr @var_70, align 2
  %conv43 = zext i16 %13 to i32
  %14 = add nuw nsw i32 %cond41, %conv43
  %sub44 = sub nsw i32 0, %14
  %div = sdiv i32 %sub34, %sub44
  %conv45 = trunc i32 %div to i8
  store i8 %conv45, ptr @var_218, align 1
  store i16 16, ptr @var_220, align 2
  %15 = load i16, ptr @var_182, align 2
  %sub88 = add nsw i32 %sub31, -1
  %tobool97.not = icmp ugt i16 %12, %15
  %lnot.ext = zext i1 %tobool97.not to i32
  %cmp98 = icmp eq i32 %sub88, %lnot.ext
  %conv100 = zext i1 %cmp98 to i16
  store i16 %conv100, ptr @var_224, align 2
  store i16 127, ptr @var_226, align 2
  br label %if.end

if.end:
  ret void
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(readwrite, argmem: none, inaccessiblemem: none, target_mem0: none, target_mem1: none) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+agu,+hwloop,-bit-reversed,-circular-buffer,-simd" }

; The if.then body must be fully scheduled through postmisched: bundles are
; formed in the region, the call survives, and every one of the four
; trailing stores is present as a materialized Format E store member
; (S_SB_* = the i8 store, S_SHW_* = the three i16 stores). The stores
; themselves may be sequential (true RAW against the call result keeps them
; out of one cycle) — the contract is completeness + termination, not a
; specific packing.
; CHECK-LABEL: name: postra_auction_wide_postcall_region
; CHECK: BUNDLE
; CHECK: JAL{{(_W|_E2_[^ ]+)?}}
; CHECK: S_SB_WITH_IMM
; CHECK: S_SHW_WITH_IMM
; CHECK: S_SHW_WITH_IMM
; CHECK: S_SHW_WITH_IMM
