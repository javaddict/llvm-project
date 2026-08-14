//===- HaydnTestMCInstrInfo.h - MCInstrInfo for the unit tests --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Format E's packer has two independent occupancy axes: SLOT says where in the
// bundle, UNIT says which hardware serves it, and neither implies the other
// (FORMAT-E-SWITCH-PLAN.md 7.1). The unit is read off the member name, so the
// packer needs an MCInstrInfo to claim one — a `Bundle` built on a plain
// `HaydnMCFormats` claims no unit and silently packs slot-only.
//
// That left unit-aware packing with NO unit-test coverage, recorded in 5.7 as
// "HaydnTests cannot currently construct an MCInstrInfo". It can: HaydnDesc is
// already linked and registers one. This is that constructor, so a test can
// choose which axis it is exercising instead of always getting the weaker one.
//
// Tests that deliberately pin slot-only behaviour keep using HaydnMCFormats.
//
#ifndef LLVM_UNITTESTS_TARGET_HAYDN_HAYDNTESTMCINSTRINFO_H
#define LLVM_UNITTESTS_TARGET_HAYDN_HAYDNTESTMCINSTRINFO_H

#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "gtest/gtest.h"

extern "C" void LLVMInitializeHaydnTargetInfo();
extern "C" void LLVMInitializeHaydnTargetMC();

namespace llvm {
namespace haydn {
namespace test {

/// The target's real MCInstrInfo, built once. Registration is idempotent, so
/// every caller can ask without ordering between test cases.
inline const MCInstrInfo &getMCInstrInfo() {
  static const MCInstrInfo *const Info = [] {
    LLVMInitializeHaydnTargetInfo();
    LLVMInitializeHaydnTargetMC();
    std::string Error;
    const Target *T = TargetRegistry::lookupTarget("haydn", Error);
    // A null target here means HaydnInfo/HaydnDesc did not get linked in;
    // failing loudly beats every unit-axis test quietly reverting to
    // slot-only, which is the exact failure mode this header exists to end.
    EXPECT_NE(T, nullptr) << "haydn target not registered: " << Error;
    return T ? T->createMCInstrInfo() : nullptr;
  }();
  return *Info;
}

} // namespace test
} // namespace haydn
} // namespace llvm

#endif // LLVM_UNITTESTS_TARGET_HAYDN_HAYDNTESTMCINSTRINFO_H
