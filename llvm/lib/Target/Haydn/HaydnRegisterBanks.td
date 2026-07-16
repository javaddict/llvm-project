//===-- HaydnRegisterBanks.td - Haydn Register Banks -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// GlobalISel register bank definitions for the Haydn DSP.
// DR64 is a separate bank from GPR32 — NOT aliased to GPR pairs (hard constraint #2).
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// GlobalISel Register Banks
// DR64 is a separate bank from GPR32 — NOT aliased to GPR pairs (§2.2)
//===----------------------------------------------------------------------===//

def GPR32RegBank : RegisterBank<"GPR32RegBank",
  [GPR32, GPR32NoSPNoLR]
>;

def DR64RegBank : RegisterBank<"DR64RegBank",
  [DR64]
>;

def ARRegBank : RegisterBank<"ARRegBank",
  [AR]
>;
