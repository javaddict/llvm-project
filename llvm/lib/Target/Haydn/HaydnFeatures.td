//===-- HaydnFeatures.td - Haydn Subtarget Features --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Subtarget features and instruction predicates for the Haydn DSP.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Subtarget features and instruction predicates.
//===----------------------------------------------------------------------===//

def FeatureAGU
    : SubtargetFeature<"agu", "HasAGU", "true",
                       "Enable Address Generation Unit (AR registers)">;
def HasAGU : Predicate<"Subtarget->hasAGU()">,
             AssemblerPredicate<(all_of FeatureAGU),
             "Enable Address Generation Unit">;

// Tuner / ProcessorModel flags only. Instruction defs are not predicated on
// HasCircularBuffer / HasBitReversed / HasSIMD: the generic CPU still
// admits those families (HaydnGeneric.td: generic = HWLoop+AGU; haydn adds
// the three). Attaching Predicates is a separate product gate, not implied
// by declaring the feature.
def FeatureCircularBuffer
    : SubtargetFeature<"circular-buffer", "HasCircularBuffer", "true",
                       "Enable circular buffer addressing (CBR registers)">;
def HasCircularBuffer : Predicate<"Subtarget->hasCircularBuffer()">,
                        AssemblerPredicate<(all_of FeatureCircularBuffer),
                        "Enable circular buffer addressing">;

def FeatureBitReversed
    : SubtargetFeature<"bit-reversed", "HasBitReversed", "true",
                       "Enable bit-reversed addressing">;
def HasBitReversed : Predicate<"Subtarget->hasBitReversed()">,
                     AssemblerPredicate<(all_of FeatureBitReversed),
                     "Enable bit-reversed addressing">;

// ISA capability only. Does not enable HardwareLoops formation;
// product default is HaydnTargetMachine::hardwareLoopsProductDefaultEnabled().
def FeatureHWLoop
    : SubtargetFeature<"hwloop", "HasHWLoop", "true",
                       "Enable hardware loop support">;
def HasHWLoop : Predicate<"Subtarget->hasHWLoop()">,
                AssemblerPredicate<(all_of FeatureHWLoop),
                "Enable hardware loops">;

def FeatureSIMD
    : SubtargetFeature<"simd", "HasSIMD", "true",
                       "Enable SIMD X2/X4 operations">;
def HasSIMD : Predicate<"Subtarget->hasSIMD()">,
              AssemblerPredicate<(all_of FeatureSIMD),
              "Enable SIMD operations">;

//===----------------------------------------------------------------------===//
// ABI / register-policy features (optional, like -fomit-frame-pointer)
//===----------------------------------------------------------------------===//

// Force a dedicated frame pointer (R14), like -fno-omit-frame-pointer.
// Default OFF: FP only when ABI needs it (VLAs, realign, llvm.frameaddress)
// or when the driver passes -fno-omit-frame-pointer.
// Enable with -mattr=+frame-pointer.
def FeatureFramePointer
    : SubtargetFeature<"frame-pointer", "UseFramePointer", "true",
                       "Force dedicated frame pointer register (R14)">;
