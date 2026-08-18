//===-- HaydnGeneric.td - Generic Haydn defs (no decode variants) --------===//
//
// Shared generic content included by BOTH:
// Haydn.td (generic + Format E product composites + residual `_S*` FU members)
// HaydnAsmMatcher.td (same generic + FLEX; no composite for matcher)
//
// Mode-0/3 decode-variant islands (HaydnInstrInfoM0/MACM0/M3/S*M0.td) and
// Bundle128 composites are deleted. Product composites are Format E only.
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Subtarget features
//===----------------------------------------------------------------------===//
include "HaydnFeatures.td"

//===----------------------------------------------------------------------===//
// Register file description
//===----------------------------------------------------------------------===//
include "HaydnRegisterInfo.td"
include "HaydnRegisterBanks.td"

//===----------------------------------------------------------------------===//
// Scheduling itineraries (must precede instruction formats so itinerary
// class names like Slot0_ALU are available for format class assignments)
//===----------------------------------------------------------------------===//
include "HaydnSchedule.td"

//===----------------------------------------------------------------------===//
// Instruction formats
//===----------------------------------------------------------------------===//
include "HaydnInstrFormats.td"
include "HaydnInstrFormatsC.td"
// FE8: Mode-0/2/3 D-class format classes (HaydnInstrFormatsD.td) deleted —
// zero product consumers.

//===----------------------------------------------------------------------===//
// Calling conventions
//===----------------------------------------------------------------------===//
include "HaydnCallingConv.td"

//===----------------------------------------------------------------------===//
// Instructions (generic — decode-variant files are NOT included here; each
// downstream.td adds them as needed)
//===----------------------------------------------------------------------===//
include "HaydnInstrInfo.td"
// Hand-maintained hypothesized encodings (filename is historical;
// generate_format_e_records.py does not emit this file).
include "HaydnInstrInfoAuto.td"

//===----------------------------------------------------------------------===//
// GlobalISel patterns
//===----------------------------------------------------------------------===//
include "HaydnGISel.td"

//===----------------------------------------------------------------------===//
// GlobalISel target generic opcodes (e.g. G_MULA64). Separate from
// HaydnGISel.td (which holds Pat patterns). See HaydnInstrGISel.td.
//===----------------------------------------------------------------------===//
include "HaydnInstrGISel.td"

//===----------------------------------------------------------------------===//
// GlobalISel combiner rules
//===----------------------------------------------------------------------===//
include "HaydnCombine.td"

//===----------------------------------------------------------------------===//
// Intrinsic ISel patterns (llvm.haydn.* -> machine instructions)
//===----------------------------------------------------------------------===//
include "HaydnIntrinsics.td"

//===----------------------------------------------------------------------===//
// Processor models
//===----------------------------------------------------------------------===//

// Generic processor - the default. FeatureHWLoop + FeatureAGU are ISA
// baseline: post/pre-inc fuse (G_HAYDN_*INC_*) is the sole update-addr
// path and is tested with the default triple. FeatureHWLoop does not
// enable HardwareLoops formation; that product default stays OFF.
// CircularBuffer / BitReversed / SIMD stay opt-in. Disable AGU densify
// with -mattr=-agu. AIE model: R12 is a normal allocatable GPR (no
// dedicated free MatInt AT).
def : ProcessorModel<"generic", HaydnSchedModel, [FeatureHWLoop, FeatureAGU]>;

// Full Haydn processor with optional ISA features.
def : ProcessorModel<"haydn", HaydnSchedModel, [FeatureAGU, FeatureCircularBuffer,
                                              FeatureBitReversed, FeatureHWLoop,
                                              FeatureSIMD]>;

//===----------------------------------------------------------------------===//
// Target definition
//===----------------------------------------------------------------------===//

def HaydnInstrInfo : InstrInfo {
  let guessInstructionProperties = 1;
}

// Target-specific replacements for pseudo instructions using PointerLikeRegClass
defm : RemapAllTargetPseudoPointerOperands<GPR32>;

def HaydnAsmWriter : AsmWriter {
  int PassSubtarget = 1;
}

def Haydn : Target {
  let InstructionSet = HaydnInstrInfo;
  let AssemblyWriters = [HaydnAsmWriter];
}
