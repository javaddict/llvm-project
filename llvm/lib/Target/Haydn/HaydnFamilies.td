//===-- HaydnFamilies.td - Bundle-format family identity -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Authored bundle-format family identity.
// Geometry / indicator bits / encodings / slot law come only from the
// family's golden nine-file set, never from this file.
//
//===----------------------------------------------------------------------===//

class HaydnBundleFamily<string name, int id> {
  string Name = name;
  int FamilyID = id;
}

// E96 is the only admitted family. A second family is an admission
// transaction (golden + STATUS pin + generator instance + inert tables),
// not a line in this file with invented bits.
def HaydnFamilyE96 : HaydnBundleFamily<"E96", 0>;
