//===-- Definition of type __jmp_buf --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES___JMP_BUF_H
#define LLVM_LIBC_TYPES___JMP_BUF_H

// TODO: implement sigjmp_buf related functions for other architectures
// Issue: https://github.com/llvm/llvm-project/issues/136358
#if defined(__linux__)
#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) ||        \
    defined(__arm__) || defined(__riscv)
#define __LIBC_HAS_SIGJMP_BUF
#endif
#endif

#if defined(__LIBC_HAS_SIGJMP_BUF)
#include "sigset_t.h"
#endif

typedef struct {
#ifdef __x86_64__
  __UINT64_TYPE__ rbx;
  __UINT64_TYPE__ rbp;
  __UINT64_TYPE__ r12;
  __UINT64_TYPE__ r13;
  __UINT64_TYPE__ r14;
  __UINT64_TYPE__ r15;
  __UINTPTR_TYPE__ rsp;
  __UINTPTR_TYPE__ rip;
#elif defined(__i386__)
  long ebx;
  long esi;
  long edi;
  long ebp;
  long esp;
  long eip;
#elif defined(__riscv)
  /* Program counter.  */
  long int __pc;
  /* Callee-saved registers.  */
  long int __regs[12];
  /* Stack pointer.  */
  long int __sp;
  /* Callee-saved floating point registers.  */
#if __riscv_float_abi_double
  double __fpregs[12];
#elif defined(__riscv_float_abi_single)
#error "__jmp_buf not available for your target architecture."
#endif
#elif defined(__arm__)
  // r4, r5, r6, r7, r8, r9, r10, r11, r12, lr
  long opaque[10];
#elif defined(__aarch64__)
  long opaque[14]; // x19-x29, lr, sp, optional x18
#if __ARM_FP
  long fopaque[8]; // d8-d15
#endif
#elif defined(__haydn__) || defined(__HAYDN__)
  // Callee-saved GPRs + return state. Offsets must match
  // libc/src/setjmp/haydn/{setjmp,longjmp}.cpp:
  //   0:r8  4:r9  8:r10  12:r11  16:sp  20:lr
  //   24:d8  32:d9  40:d10  48:d11  56:d12  64:d13  72:d14  80:d15
  long r8;
  long r9;
  long r10;
  long r11;
  long sp;
  long lr;
  // Callee-saved FP regs d8-d15 (64-bit each). Placed after 24-byte GPR
  // block so natural long-long alignment needs no padding.
  long long dregs[8];
#else
#error "__jmp_buf not available for your target architecture."
#endif
#if defined(__LIBC_HAS_SIGJMP_BUF)
  // return address
  void *sig_retaddr;
  // extra register buffer to avoid indefinite stack growth in sigsetjmp
  void *sig_extra;
  // signal masks
  sigset_t sigmask;
#endif
} __jmp_buf;

#undef __LIBC_HAS_SIGJMP_BUF

#endif // LLVM_LIBC_TYPES___JMP_BUF_H
