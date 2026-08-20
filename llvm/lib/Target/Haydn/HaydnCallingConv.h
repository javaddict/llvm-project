//===-- HaydnCallingConv.h - Haydn Custom CC Routines ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the custom routines for the Haydn Calling Convention.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNCALLINGCONV_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNCALLINGCONV_H

#include "llvm/CodeGen/CallingConvLower.h"

namespace llvm {

// Entry=1 in HaydnCallingConv.td emits these in the llvm namespace
// (PPCCallingConv.h:22 / PPCCallingConv.td:44). HaydnCallingConv.cpp is
// the single HaydnGenCallingConv.inc owner.

bool CC_Haydn(unsigned ValNo, MVT ValVT, MVT LocVT,
              CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
              Type *OrigTy, CCState &State);

bool RetCC_Haydn(unsigned ValNo, MVT ValVT, MVT LocVT,
                 CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                 Type *OrigTy, CCState &State);

// R0 is reserved software zero — never an argument or return location.
bool HaydnLocIsReservedSoftZero(unsigned Reg);

// i128 / half / bfloat are not CC types (fail-closed; not a product C ABI).
bool HaydnCCAssignRejectsType(MVT ValVT);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNCALLINGCONV_H
