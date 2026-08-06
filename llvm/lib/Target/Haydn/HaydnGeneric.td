//===-- HaydnGeneric.td - Generic Haydn defs (no decode variants) --------===//
//
// Shared generic content included by BOTH:
// Haydn.td (generic + FLEX formats / Bundle128;)
// HaydnAsmMatcher.td (same generic + FLEX; no composite for matcher)
//
// Mode-0/3 decode-variant islands (HaydnInstrInfoM0/MACM0/M3/S*M0.td) were
// retired by /R4 and archived under legacy-retired/ by. Live slot
// encoding is exclusively `_S<k>` + BUNDLE128_FULL.
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
// Unit scheduling model (generated), before the itineraries that
// concatenate it: it defines FuncUnits and itinerary classes only and
// claims no alternates index, so it can land while Bundle128 is live.
include "HaydnFormatESchedule.td"
include "HaydnSchedule.td"

//===----------------------------------------------------------------------===//
// Instruction formats
//===----------------------------------------------------------------------===//
include "HaydnInstrFormats.td"
include "HaydnInstrFormatsC.td"
// HaydnInstrFormatsD.td (FmtM0_*/FmtM2_*/FmtM3_*) un-included — Mode-0/2/3
// format classes had zero live instruction defs after shell retirement. File
// remains on disk for history; island instruction files are under legacy-retired/.

//===----------------------------------------------------------------------===//
// Calling conventions
//===----------------------------------------------------------------------===//
include "HaydnCallingConv.td"

//===----------------------------------------------------------------------===//
// Instructions (generic — decode-variant files are NOT included here; each
// downstream.td adds them as needed)
//===----------------------------------------------------------------------===//
include "HaydnInstrInfo.td"
include "HaydnInstrInfoC.td"
include "HaydnInstrInfoAuto.td"

//===----------------------------------------------------------------------===//
// GlobalISel patterns (populated in Stream D0)
//===----------------------------------------------------------------------===//
include "HaydnGISel.td"

//===----------------------------------------------------------------------===//
// GlobalISel target generic opcodes (e.g. G_MULA64). Separate from
// HaydnGISel.td (which holds Pat patterns). See HaydnInstrGISel.td.
//===----------------------------------------------------------------------===//
include "HaydnInstrGISel.td"

//===----------------------------------------------------------------------===//
// GlobalISel combiner rules (populated in Stream D0)
//===----------------------------------------------------------------------===//
include "HaydnCombine.td"

//===----------------------------------------------------------------------===//
// Intrinsic ISel patterns (llvm.haydn.* -> machine instructions)
//===----------------------------------------------------------------------===//
include "HaydnIntrinsics.td"

//===----------------------------------------------------------------------===//
// Processor models
//===----------------------------------------------------------------------===//

// Generic processor - the default. HWLoop + AGU are product baseline:
// post/pre-inc fuse (G_HAYDN_*INC_*) is the sole update-addr path and is
// tested with the default triple. CircularBuffer / BitReversed / SIMD stay
// opt-in. Disable AGU densify with -mattr=-agu (T7.3 feature contract).
// AIE model: R12 is a normal allocatable GPR (no dedicated free MatInt AT).
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
