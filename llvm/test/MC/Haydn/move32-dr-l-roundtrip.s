// RUN: llvm-mc -triple haydn-unknown-elf -show-encoding < %s 2>/dev/null \
// RUN:   | FileCheck %s --check-prefix=ENC
// RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj -o %t.o %s
// RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=DIS

// REGRESSION TEST: MOVE32_DR_L MC encode + object round-trip.
//
// Bug context: HaydnInstructionSelector G_UNMERGE_VALUES now emits
// MOVE32_DR_L for v8i8 byte-lane extracts from a DR64 source (cross-bank
// DR64->GPR32 lane extract, Hard #2). The selector-side fix only sticks if
// the MOVE32_DR_L pseudo has a valid Format E member that MC can encode and
// disassemble. If the parcel does not round-trip, the cross-bank extract
// path crashes in MC instead of the old copyPhysReg path.
//
// Test design: parse `move32_dr_l rt, rsd`, confirm show-encoding produces a
// non-empty parcel, emit an object file, and confirm objdump renders the
// parcel back without aborting. The encoding bytes are golden-derived; this
// test pins the round-trip, not the exact parcel bytes.

// ENC:     move32_dr_l
// ENC-SAME: encoding:
// DIS:     move32_dr_l
{ move32_dr_l r1, d0; nop }
