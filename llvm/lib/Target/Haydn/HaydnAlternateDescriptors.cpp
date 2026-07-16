//===- HaydnAlternateDescriptors.cpp - Alt descriptor side-map --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// `HaydnAlternateDescriptors` is a header-only side-map (all methods are
// inline). This translation unit exists solely to anchor the header in the
// build so it is type-checked today — it has no consumers yet (steps 2-4
// fill it; SMS reads it). Without an anchoring TU the header would not be
// compiled until the first includer lands, risking a latent build break.
//
//===----------------------------------------------------------------------===//

#include "HaydnAlternateDescriptors.h"

namespace llvm {
// Force the header to be parsed/compiled as part of HaydnCodeGen. The class is
// fully defined in the header; no out-of-line definitions live here.
} // namespace llvm
