//===-- HaydnRegisterInfo.td - Haydn Register defs ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Register definitions for the Haydn DSP.
// 4-bit HW encoding for GPR and DR.
// Separate register banks per hard constraint #2.
//
//===----------------------------------------------------------------------===//

let Namespace = "Haydn" in {

//===----------------------------------------------------------------------===//
// Register class definitions
//===----------------------------------------------------------------------===//

class HaydnReg<bits<4> Enc, string n, list<string> alt = []> : Register<n> {
  let HWEncoding{3 - 0} = Enc;
  let AltNames = alt;
}

//===----------------------------------------------------------------------===//
// GPR32: General Purpose Registers (R0-R15)
// R0=soft-zero (reserved), R12=AT linker/assembler scratch (reserved)
// R13=SP (always reserved), R14=FP (reserved when hasFP), R15=LR (always reserved).
// Allocatable GPRs: R1-R11.
//===----------------------------------------------------------------------===//

def R0  : HaydnReg< 0, "r0">,  DwarfRegNum<[0]>;
def R1  : HaydnReg< 1, "r1">,  DwarfRegNum<[1]>;
def R2  : HaydnReg< 2, "r2">,  DwarfRegNum<[2]>;
def R3  : HaydnReg< 3, "r3">,  DwarfRegNum<[3]>;
def R4  : HaydnReg< 4, "r4">,  DwarfRegNum<[4]>;
def R5  : HaydnReg< 5, "r5">,  DwarfRegNum<[5]>;
def R6  : HaydnReg< 6, "r6">,  DwarfRegNum<[6]>;
def R7  : HaydnReg< 7, "r7">,  DwarfRegNum<[7]>;
def R8  : HaydnReg< 8, "r8">,  DwarfRegNum<[8]>;
def R9  : HaydnReg< 9, "r9">,  DwarfRegNum<[9]>;
def R10 : HaydnReg<10, "r10">, DwarfRegNum<[10]>;
def R11 : HaydnReg<11, "r11">, DwarfRegNum<[11]>;
def R12 : HaydnReg<12, "r12">, DwarfRegNum<[12]>;  // AT (linker/assembler scratch, reserved —)
def R13 : HaydnReg<13, "sp">,  DwarfRegNum<[13]>;  // Stack Pointer
def R14 : HaydnReg<14, "fp">,  DwarfRegNum<[14]>;  // Frame Pointer (when used)
def R15 : HaydnReg<15, "lr">,  DwarfRegNum<[15]>;  // Link Register

//===----------------------------------------------------------------------===//
// DR64: Data Registers (D0-D15) — separate bank, NOT aliased to GPR pairs
//===----------------------------------------------------------------------===//

def D0  : HaydnReg< 0, "d0">,  DwarfRegNum<[16]>;
def D1  : HaydnReg< 1, "d1">,  DwarfRegNum<[17]>;
def D2  : HaydnReg< 2, "d2">,  DwarfRegNum<[18]>;
def D3  : HaydnReg< 3, "d3">,  DwarfRegNum<[19]>;
def D4  : HaydnReg< 4, "d4">,  DwarfRegNum<[20]>;
def D5  : HaydnReg< 5, "d5">,  DwarfRegNum<[21]>;
def D6  : HaydnReg< 6, "d6">,  DwarfRegNum<[22]>;
def D7  : HaydnReg< 7, "d7">,  DwarfRegNum<[23]>;
def D8  : HaydnReg< 8, "d8">,  DwarfRegNum<[24]>;
def D9  : HaydnReg< 9, "d9">,  DwarfRegNum<[25]>;
def D10 : HaydnReg<10, "d10">, DwarfRegNum<[26]>;
def D11 : HaydnReg<11, "d11">, DwarfRegNum<[27]>;
def D12 : HaydnReg<12, "d12">, DwarfRegNum<[28]>;
def D13 : HaydnReg<13, "d13">, DwarfRegNum<[29]>;
def D14 : HaydnReg<14, "d14">, DwarfRegNum<[30]>;
def D15 : HaydnReg<15, "d15">, DwarfRegNum<[31]>;

//===----------------------------------------------------------------------===//
// AR: Aligned Registers (AR0–AR3) — 4×64-bit AGU residual streams
// Restored to the full 2-bit ar_sel domain (CB-149): the golden field is
// 2 bits, BundleSim's semantic model executes int64_t ar[4], the NatureDSP
// compat surface documents ar&=3, and this line's own pre-retirement Sema
// tests expected [0, 3]. The interim AR0/AR1-only retirement was fail-closed
// pending a golden classification of codes 2..3 that never arrived; the
// execution oracle is the classification we have.
//===----------------------------------------------------------------------===//

class HaydnARReg<bits<2> Enc, string n> : Register<n> {
  let HWEncoding{1 - 0} = Enc;
}

def AR0 : HaydnARReg<0, "ar0">, DwarfRegNum<[32]>;
def AR1 : HaydnARReg<1, "ar1">, DwarfRegNum<[33]>;
def AR2 : HaydnARReg<2, "ar2">, DwarfRegNum<[34]>;
def AR3 : HaydnARReg<3, "ar3">, DwarfRegNum<[35]>;

//===----------------------------------------------------------------------===//
// Control/Special Registers
//===----------------------------------------------------------------------===//

def SFR : HaydnReg<0, "sfr">, DwarfRegNum<[36]>;  // Status Flag Register (4-bit)
def CSR : HaydnReg<0, "csr">, DwarfRegNum<[37]>;  // Control/Status Register (32-bit)

// Circular Buffer Registers (CBR) — 2 sets, selected by cbr_sel (0 or 1).
// Per the manual there is NO CBR_SIZE register: each set is a pair
// CBR_BEGIN[cbr_sel] — inclusive start byte address (CSR 0x2C / 0x2E)
// CBR_END[cbr_sel] — inclusive last-byte address (CSR 0x2D / 0x2F)
// and the buffer size is derived as CBR_END - CBR_BEGIN + 1. Both halves are
// programmed via CSRW before any circular-buffer load/store is issued. These
// are CSRs, not GPRs/DRs — they are never allocatable and are only ever
// written implicitly through CSRW (see SETCBR_BEGIN / SETCBR_END pseudos).
def CBR0 : HaydnReg<0, "cbr0">, DwarfRegNum<[38]>;  // set 0: {CBR_BEGIN[0], CBR_END[0]}
def CBR1 : HaydnReg<1, "cbr1">, DwarfRegNum<[39]>;  // set 1: {CBR_BEGIN[1], CBR_END[1]}

} // Namespace = "Haydn"

//===----------------------------------------------------------------------===//
// Register Classes
//===----------------------------------------------------------------------===//

// GPR32 contains every GPR for instruction-encoding/operand-class purposes
// (the register scavenger and some pseudo expansions need to name any GPR).
// R13 (SP), R14 (FP), R15 (LR) are members so operands can name them.
// getReservedRegs keeps R0/R13/R15 (and R14 when hasFP) out of the
// allocatable set. Allocatable GPRs: R1–R12 (R12 is a normal caller-saved
// GPR; AIE model has no free AT).
def GPR32 : RegisterClass<"Haydn",
  [i32, f32], 32, (add
    (sequence "R%u", 0, 11), R12, R13, R14, R15
  )> {
  let RegInfos = RegInfoByHwMode<
    [DefaultMode],
    [RegInfo<32, 32, 32>]   // size, spill size, alignment
  >;
}

// Allocatable GPR class excluding SP and LR. Includes R12 as a normal
// caller-saved GPR. R14 excluded only via hasFP reservation.
def GPR32NoSPNoLR : RegisterClass<"Haydn",
  [i32, f32], 32, (add
    (sequence "R%u", 0, 12), R14
  )> {
  let RegInfos = RegInfoByHwMode<
    [DefaultMode],
    [RegInfo<32, 32, 32>]
  >;
}

def DR64 : RegisterClass<"Haydn",
  [i64, f64, v2i32, v2f32, v4i16], 64, (add
    (sequence "D%u", 0, 15)
  )> {
  let RegInfos = RegInfoByHwMode<
    [DefaultMode],
    [RegInfo<64, 64, 64>]  // 64-bit size, 64-bit alignment
  >;
}

def AR : RegisterClass<"Haydn",
  [i64], 64, (add AR0, AR1, AR2, AR3)
> {
  let RegInfos = RegInfoByHwMode<
    [DefaultMode],
    [RegInfo<64, 64, 64>]
  >;
}

// Low GPR subset for compressed 16-bit instructions (3-bit register encoding).
// Only R0-R7 are eligible for compressed forms.
//
// soft RA hints (-haydn-ra-compact-hints): getRegAllocationHints may prefer
// physregs in this class (and low-DR encodings D0-D7) after stronger base
// copy/coalesce hints. Soft order only — never demote a Full-bank virtreg RC
// to a hard GPR32Lo constraint; wide AllocationOrder remains complete.
def GPR32Lo : RegisterClass<"Haydn",
  [i32, f32], 32, (add
    (sequence "R%u", 0, 7)
  )> {
  let RegInfos = RegInfoByHwMode<
    [DefaultMode],
    [RegInfo<32, 32, 32>]   // size, spill size, alignment
  >;
}

// Callee-saved registers defined in HaydnCallingConv.td to avoid duplicate
// CSR_Haydn is defined there as: R8-R11, R15(LR), D8-D15 (R12=AT reserved).
