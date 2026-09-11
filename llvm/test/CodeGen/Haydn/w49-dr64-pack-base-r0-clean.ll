; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -enable-misched=false -enable-post-misched=false \
; RUN:     -verify-machineinstrs < %s 2>&1 | FileCheck %s

; Role: semantic — withDR64PackBase must restore soft-zero R0 before MatInt.
;
; withDR64PackBase's !isInt<20>(Off) arm seeds Cur=R0. A mid-function write
; (inline-asm r0 def here) leaves R0 dirty; the pack-base helper must emit a
; local XOR32 restore and then assert cleanliness. Never a function-wide
; epilogue xor; never silently read a dirty seed. Base stays a scavenged
; non-R0 GPR. Peer: AIE has no soft-zero R0; Hexagon packetizers do not
; borrow a reserved zero as a MatInt seed.
;
; Test design: VLA forces hasFP. A >simm20 local ([150000 x i32]) pushes
; DR64PackFI's FrameReg-relative offset out of isInt<20> so the MatInt arm
; runs. Inline asm defines $r0 after the prologue zero. The both-halves-
; nonzero i64 (0x2_0000_0001) takes the DR64 pack path. Pin: at least one
; xor32 r0 after the dirty write, pack ST32/ST32/LD64 completes, no
; 8-byte SP transient, base of LUI is not r0.

; CHECK-NOT: LLVM ERROR

define i64 @w49_pack_base_r0_clean(i32 %a, i32 %n) nounwind {
; CHECK-LABEL: w49_pack_base_r0_clean:
; Prologue soft-zero, then the explicit dirty write of r0.
; CHECK:       xor32{{.*}}r0, r0, r0
; Dirty write of r0 (COPY/MOVE32 of %a, or an addi32 from the asm).
; CHECK:       {{addi32|move32|ori32}}{{.*}}r0
; Local restore immediately before MatInt seed. LUI dest is the scavenged
; pack base, never r0.
; Pack: two ST32 halves + LD64. No 8-byte SP transient.
; CHECK-DAG:   st32
; CHECK-DAG:   ld64
; CHECK:       xor32{{.*}}r0, r0, r0
; CHECK:       {{lui|ori32}}{{.*}}r{{[1-9]|1[0-2]}}
; Pack-base dest is never r0. Do not match epilogue `ori32 rN, r0, imm`
; after the intentional soft-zero restore.
; CHECK-NOT:   {{lui|ori32}} r0,
; CHECK-NOT:   subi32{{.*}}sp,{{.*}}sp, 8
; CHECK-NOT:   addi32{{.*}}sp,{{.*}}sp, 8
entry:
  %slot = alloca i32, i32 %n
  %arr = alloca [150000 x i32], align 8
  %dirty = call i32 asm sideeffect "", "={r0},r"(i32 %a)
  %cmp = icmp eq i32 %a, 0
  %sel = select i1 %cmp, i64 8589934593, i64 0
  store volatile i32 %dirty, ptr %slot
  store volatile i32 %a, ptr %arr
  ret i64 %sel
}
