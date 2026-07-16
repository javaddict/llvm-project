//===-- HaydnMCAsmInfo.h - Haydn Asm Info ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the HaydnMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCASMINFO_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class HaydnMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit HaydnMCAsmInfo(const Triple &TargetTriple);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMCASMINFO_H
