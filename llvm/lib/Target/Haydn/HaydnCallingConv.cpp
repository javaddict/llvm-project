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
// This defines CC_Haydn and RetCC_Haydn functions.
#include "HaydnGenCallingConv.inc"
