; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -stop-after=instruction-select -verify-machineinstrs -o - < %s | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs -o - < %s | FileCheck %s --check-prefix=ASM
; RUN: %python -c "import pathlib, shutil, sys; p=pathlib.Path(shutil.which('llc')).resolve().parent.parent/'lib'/'Target'/'Haydn'/'HaydnGenInstrInfo.inc'; sys.stdout.write(p.read_text())" | FileCheck %s --check-prefix=DESC

; Role: semantic — ST8/16/32/64 descriptors are MayStore only (not MayLoad).
;
; REGRESSION TEST: FmtLS stores must not inherit mayLoad=1.
;
; Bug: class FmtLS in HaydnInstrFormats.td defaults mayLoad=1. ST8/ST16/ST32/ST64
; set mayStore=1 but never cleared mayLoad, so generated MCInstrDesc rows were
; MayLoad|MayStore. That poisoned AA, HaydnInstrInfo::isStoreToStackSlot
; (requires mayStore && !mayLoad), and intra-cycle RAW.
;
; Fix: per-instruction `let mayLoad = 0` on FmtLS stores (and store members in
; HaydnFormatsLS.td). Loads keep FmtLS mayLoad=1. If this regresses, DESC CHECKs
; fail because ST* rows regain MayLoad|(MayStore), and disjoint LD32+ST32 may
; stop co-issuing.
;
; Test design: (1) ISel MIR pins ST* as store MMOs and LD32 as a load MMO
; (loads still load). (2) Generated HaydnGenInstrInfo.inc rows pin
; MayStore-without-MayLoad on ST8/16/32/64 and MayLoad on LD32. (3) Disjoint
; noalias LD32+ST32 must share one bundle (S0 store + S1 load) without inventing
; scheduler flags.

; DESC-DAG: 0|(1ULL<<MCID::MayStore)|(1ULL<<MCID::ExtraSrcRegAllocReq)|(1ULL<<MCID::ExtraDefRegAllocReq), {{.*}} },  // ST8{{$}}
; DESC-DAG: 0|(1ULL<<MCID::MayStore)|(1ULL<<MCID::ExtraSrcRegAllocReq)|(1ULL<<MCID::ExtraDefRegAllocReq), {{.*}} },  // ST16{{$}}
; DESC-DAG: 0|(1ULL<<MCID::MayStore)|(1ULL<<MCID::ExtraSrcRegAllocReq)|(1ULL<<MCID::ExtraDefRegAllocReq), {{.*}} },  // ST32{{$}}
; DESC-DAG: 0|(1ULL<<MCID::MayStore)|(1ULL<<MCID::ExtraSrcRegAllocReq)|(1ULL<<MCID::ExtraDefRegAllocReq), {{.*}} },  // ST64{{$}}
; DESC-DAG: 0|(1ULL<<MCID::MayLoad)|(1ULL<<MCID::ExtraSrcRegAllocReq)|(1ULL<<MCID::ExtraDefRegAllocReq), {{.*}} },  // LD32{{$}}

define void @store_i8(ptr %p, i8 %v) {
  store i8 %v, ptr %p, align 1
  ret void
}
; MIR-LABEL: name: store_i8
; MIR: ST8 {{.*}} :: (store (s8) into %ir.p

define void @store_i16(ptr %p, i16 %v) {
  store i16 %v, ptr %p, align 2
  ret void
}
; MIR-LABEL: name: store_i16
; MIR: ST16 {{.*}} :: (store (s16) into %ir.p

define void @store_i32(ptr %p, i32 %v) {
  store i32 %v, ptr %p, align 4
  ret void
}
; MIR-LABEL: name: store_i32
; MIR: ST32 {{.*}} :: (store (s32) into %ir.p

define void @store_i64(ptr %p, i64 %v) {
  store i64 %v, ptr %p, align 8
  ret void
}
; MIR-LABEL: name: store_i64
; MIR: ST64 {{.*}} :: (store (s64) into %ir.p

define i32 @load_i32(ptr %p) {
  %v = load i32, ptr %p, align 4
  ret i32 %v
}
; MIR-LABEL: name: load_i32
; MIR: LD32 {{.*}} :: (load (s32) from %ir.p

define i32 @disjoint_ld32_st32(ptr noalias %p, ptr noalias %q, i32 %v) {
  store i32 %v, ptr %q, align 4
  %ld = load i32, ptr %p, align 4
  ret i32 %ld
}
; MIR-LABEL: name: disjoint_ld32_st32
; MIR: ST32 {{.*}} :: (store (s32) into %ir.q
; MIR: LD32 {{.*}} :: (load (s32) from %ir.p
;
; ASM-LABEL: disjoint_ld32_st32:
; ASM: { {{[^}\n]*}}{{ld32|st32}}{{[^}\n]*}}{{ld32|st32}}{{[^}\n]*}} }
