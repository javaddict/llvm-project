//===-- HaydnTargetInfo.h - Haydn Target Implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides Haydn target registration.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_TARGETINFO_HAYDNTARGETINFO_H
#define LLVM_LIB_TARGET_HAYDN_TARGETINFO_HAYDNTARGETINFO_H

namespace llvm {

class Target;

Target &getTheHaydnTarget();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_TARGETINFO_HAYDNTARGETINFO_H
