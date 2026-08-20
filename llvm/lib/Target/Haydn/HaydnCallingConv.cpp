//===-- HaydnCallingConv.cpp - Haydn Custom CC Routines ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the table-generated routines for the Haydn Calling Convention.
//
//===----------------------------------------------------------------------===//

#include "HaydnCallingConv.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"

using namespace llvm;

// Include the tablegen-generated calling convention implementation.
// Entry=1 in HaydnCallingConv.td emits llvm::CC_Haydn / llvm::RetCC_Haydn
// (PPCCallingConv.cpp:196 / PPCCallingConv.td:44). GISel CallLowering
// includes HaydnCallingConv.h rather than a second copy of this .inc.
#include "HaydnGenCallingConv.inc"

bool llvm::HaydnLocIsReservedSoftZero(unsigned Reg) {
  return Reg == Haydn::R0;
}

bool llvm::HaydnCCAssignRejectsType(MVT ValVT) {
  return ValVT == MVT::i128 || ValVT == MVT::f16 || ValVT == MVT::bf16;
}
