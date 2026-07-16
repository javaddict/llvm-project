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

// This is used for assigning arguments to locations when making calls.
bool CC_Haydn(unsigned ValNo, MVT ValVT, MVT LocVT,
              CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
              Type *OrigTy, CCState &State);

// This is used for assigning return values to locations when making calls.
bool RetCC_Haydn(unsigned ValNo, MVT ValVT, MVT LocVT,
                 CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                 Type *OrigTy, CCState &State);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNCALLINGCONV_H
