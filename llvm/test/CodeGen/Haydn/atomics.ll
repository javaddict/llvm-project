; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — atomics must lower to __atomic_* libcalls, NOT plain ld/st.

; REGRESSION TEST: atomics must lower to __atomic_* libcalls, NOT plain ld/st.
;
; Bug history:
; 1) shouldExpandAtomic{Load,Store,RMW}InIR returned AtomicExpansionKind::None
; while setMaxAtomicSizeInBitsSupported(64) was set. With max-size > 0
; atomicSizeSupported returned true, so AtomicExpandPass never consulted
; the shouldExpand* hooks — atomics reached GISel untouched, which then
; crashed with "unable to legalize G_ATOMICRMW_*" (GISel's generic
; LegalizerHelper::lower has no handler for atomics).
; 2) Additionally, the libcall availability set for the unrecognized
; haydn-unknown-elf triple left ATOMIC_LOAD_4 etc. as Unsupported, so
; getLibcallName returned null and AtomicExpandPass silently stripped
; the atomic IR to `ret poison` even when MaxAtomicSize was set to 0.
;
; Fix:
; setMaxAtomicSizeInBitsSupported(0) (HaydnISelLowering.cpp) — makes
; atomicSizeSupported return false for every size, so AtomicExpandPass
; routes every atomic load/store/RMW/cmpxchg through
; expandAtomic{Load,Store,RMW,CAS}ToLibcall BEFORE consulting any
; shouldExpandAtomic*InIR hook.
; HaydnSubtarget::initLibcallLoweringInfo registers the standard
; __atomic_* compiler-rt/libgcc Impl names so getLibcallName returns
; a non-null name and the libcall is actually emitted.
;
; Test design: each atomic op must produce a call to a __atomic_* runtime
; symbol. Haydn prints calls as `jal_w <reg>, <symbol>` (VLIW slot-0 only). If
; the expansion regresses, the CHECK lines fail because plain ld32/st32
; appear instead of the libcall (or `ret poison` appears with no body at all).
; See atomics-libcall-expansion,.
;
; T-ABI7 tension (documented, not "fixed" here): Clang MaxAtomicInlineWidth=32
; advertises lock-free i32, while this backend libcalls every size. Single-hart
; BSP stubs make that sound; a second hart would not.

define i32 @atomic_load_monotonic(ptr %ptr) {
; CHECK-LABEL: atomic_load_monotonic:
; CHECK:       jal{{(\.s[012])?}} {{.*}}__atomic_load_4
  %v = load atomic i32, ptr %ptr monotonic, align 4
  ret i32 %v
}

define i32 @atomic_load_acquire(ptr %ptr) {
; CHECK-LABEL: atomic_load_acquire:
; CHECK:       jal{{(\.s[012])?}} {{.*}}__atomic_load_4
  %v = load atomic i32, ptr %ptr acquire, align 4
  ret i32 %v
}

define void @atomic_store_monotonic(ptr %ptr, i32 %val) {
; CHECK-LABEL: atomic_store_monotonic:
; CHECK:       jal{{(\.s[012])?}} {{.*}}__atomic_store_4
  store atomic i32 %val, ptr %ptr monotonic, align 4
  ret void
}

define void @atomic_store_release(ptr %ptr, i32 %val) {
; CHECK-LABEL: atomic_store_release:
; CHECK:       jal{{(\.s[012])?}} {{.*}}__atomic_store_4
  store atomic i32 %val, ptr %ptr release, align 4
  ret void
}

define i64 @atomic_load_i64(ptr %ptr) {
; CHECK-LABEL: atomic_load_i64:
; CHECK:       jal{{(\.s[012])?}} {{.*}}__atomic_load_8
  %v = load atomic i64, ptr %ptr monotonic, align 8
  ret i64 %v
}

define void @atomic_store_i64(ptr %ptr, i64 %val) {
; CHECK-LABEL: atomic_store_i64:
; CHECK:       jal{{(\.s[012])?}} {{.*}}__atomic_store_8
  store atomic i64 %val, ptr %ptr monotonic, align 8
  ret void
}

define i32 @atomic_rmw_add(ptr %ptr) {
; CHECK-LABEL: atomic_rmw_add:
; CHECK:       jal{{(\.s[012])?}} {{.*}}__atomic_fetch_add_4
  %v = atomicrmw add ptr %ptr, i32 1 monotonic
  ret i32 %v
}

define i32 @atomic_rmw_xchg(ptr %ptr) {
; CHECK-LABEL: atomic_rmw_xchg:
; CHECK:       jal{{(\.s[012])?}} {{.*}}__atomic_exchange_4
  %v = atomicrmw xchg ptr %ptr, i32 42 monotonic
  ret i32 %v
}
