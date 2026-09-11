//===-- HaydnGeneric.td - Generic Haydn defs (no decode variants) --------===//
//
// Shared generic content included by BOTH:
// Haydn.td (generic + Format E product composites)
// HaydnAsmMatcher.td (same generic; no composites, no FieldSlots)
//
// Mode-0/3 decode-variant islands and Bundle128 composites are deleted.
// Product composites are Format E only.
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
include "HaydnPseudos.td"
include "HaydnInstrInfo.td"
include "HaydnInstrInfoGolden.td.inc"


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

// generic and haydn are the same full ISA. There is no product CPU that
// turns features off: empty / -mcpu=generic / -mcpu=haydn all enable
// agu + circular-buffer + bit-reversed + hwloop + simd.
// FeatureHWLoop does not enable HardwareLoops formation (product default
// is HaydnTargetMachine::hardwareLoopsProductDefaultEnabled()).
// -mtune selects HaydnSchedModel only (TuneImplies is empty).
def : ProcessorModel<"generic", HaydnSchedModel, [FeatureAGU, FeatureCircularBuffer,
                                              FeatureBitReversed, FeatureHWLoop,
                                              FeatureSIMD]>;

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
