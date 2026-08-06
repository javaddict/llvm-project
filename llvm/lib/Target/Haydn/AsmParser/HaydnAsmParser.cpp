//===---- HaydnAsmParser.cpp - Parse Haydn assembly to MCInst instructions --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnBundle.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "TargetInfo/HaydnTargetInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

#define DEBUG_TYPE "haydn-asm-parser"

namespace {

// Auto-generated functions - included from HaydnGenAsmMatcher.inc
// These are static functions in the anonymous namespace
#define GET_REGISTER_MATCHER
#include "HaydnGenAsmMatcher.inc"

// HaydnOperand - Base class for Haydn assembly operands
class HaydnOperand : public MCParsedAsmOperand {
public:
  enum KindTy {
    k_Token,
    k_Register,
    k_Immediate,
    k_Memory
  };

private:
  KindTy Kind;

  SMLoc StartLoc, EndLoc;

  struct Token {
    const char *Data;
    unsigned Length;
  };
  struct RegOp {
    MCRegister RegVal;
  };
  struct ImmOp {
    const MCExpr *Val;
  };
  struct MemOp {
    MCRegister Base;
    const MCExpr *Offset;
  };

  union {
    Token Tok;
    RegOp Reg;
    ImmOp Imm;
    MemOp Mem;
  };

public:
  HaydnOperand(KindTy K, SMLoc S, SMLoc E)
      : Kind(K), StartLoc(S), EndLoc(E) {}

  // Token constructor
  HaydnOperand(StringRef Str, SMLoc S)
      : Kind(k_Token), StartLoc(S), EndLoc(S) {
    Tok.Data = Str.data();
    Tok.Length = Str.size();
  }

  // Register constructor
  HaydnOperand(MCRegister RegVal, SMLoc S, SMLoc E)
      : Kind(k_Register), StartLoc(S), EndLoc(E) {
    Reg.RegVal = RegVal;
  }

  // Immediate constructor
  HaydnOperand(const MCExpr *Val, SMLoc S, SMLoc E)
      : Kind(k_Immediate), StartLoc(S), EndLoc(E) {
    Imm.Val = Val;
  }

  // Memory constructor
  HaydnOperand(MCRegister Base, const MCExpr *Offset, SMLoc S, SMLoc E)
      : Kind(k_Memory), StartLoc(S), EndLoc(E) {
    Mem.Base = Base;
    Mem.Offset = Offset;
  }

  // Get the location for diagnostics.
  SMLoc getStartLoc() const override { return StartLoc; }
  SMLoc getEndLoc() const override { return EndLoc; }

  // Allow printing of operands for debugging
  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case k_Token:
      OS << "Token: " << getToken();
      break;
    case k_Register:
      OS << "Register: " << getReg().id();
      break;
    case k_Immediate:
      OS << "Immediate: ";
      if (getImm()) {
        if (const auto *CE = dyn_cast<MCConstantExpr>(getImm()))
          OS << CE->getValue();
        else
          OS << "<expr>";
      } else
        OS << "0";
      break;
    case k_Memory:
      OS << "Memory: [" << getMemBase().id();
      if (getMemOffset()) {
        OS << ", ";
        if (const auto *CE = dyn_cast<MCConstantExpr>(getMemOffset()))
          OS << CE->getValue();
        else
          OS << "<expr>";
      }
      OS << "]";
      break;
    }
  }

  StringRef getToken() const {
    assert(Kind == k_Token && "Invalid access!");
    return StringRef(Tok.Data, Tok.Length);
  }

  MCRegister getReg() const override {
    assert(Kind == k_Register && "Invalid access!");
    return Reg.RegVal;
  }

  const MCExpr *getImm() const {
    assert(Kind == k_Immediate && "Invalid access!");
    return Imm.Val;
  }

  MCRegister getMemBase() const {
    assert(Kind == k_Memory && "Invalid access!");
    return Mem.Base;
  }

  const MCExpr *getMemOffset() const {
    assert(Kind == k_Memory && "Invalid access!");
    return Mem.Offset;
  }

  // isToken/isReg/isImm/isMem - Type checking methods
  bool isToken() const override { return Kind == k_Token; }
  bool isReg() const override { return Kind == k_Register; }
  bool isImm() const override { return Kind == k_Immediate; }
  bool isMem() const override { return Kind == k_Memory; }

  // Immediate validation methods for different operand types
  bool isSImm16() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      int64_t Value = CE->getValue();
      return Value >= -(1LL << 15) && Value < (1LL << 15);
    }
    return true; // Non-constant expressions are assumed valid
  }

  bool isSImm20() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      int64_t Value = CE->getValue();
      return Value >= -(1LL << 19) && Value < (1LL << 19);
    }
    return true; // Non-constant expressions are assumed valid
  }

  // Format E I32 takes a full-width signed immediate, so every 32-bit value is
  // in range and only the width itself is checked.
  bool isSImm32() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      int64_t Value = CE->getValue();
      return Value >= -(1LL << 31) && Value < (1LL << 31);
    }
    return true; // Non-constant expressions are assumed valid
  }

  // fused post/pre-increment load scaled immediate (signed 6-bit element
  // index, range -32..+31). Mirrors isSImm16; the byte stride is recovered at
  // encode/decode time via << ScaleShift (3 for D_LDW*, 2 for S_LW*).
  bool isSImm6() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      int64_t Value = CE->getValue();
      return Value >= -(1LL << 5) && Value < (1LL << 5);
    }
    return true; // Non-constant expressions are assumed valid
  }

  // Flex scaled immediates (signed 8-bit / 12-bit element indices).
  // Mirrors isSImm6; the byte stride is recovered at encode/decode time.
  bool isSImm8() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      int64_t Value = CE->getValue();
      return Value >= -(1LL << 7) && Value < (1LL << 7);
    }
    return true; // Non-constant expressions are assumed valid
  }

  bool isSImm12() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      int64_t Value = CE->getValue();
      return Value >= -(1LL << 11) && Value < (1LL << 11);
    }
    return true; // Non-constant expressions are assumed valid
  }

  bool isUImm1() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      uint64_t Value = CE->getValue();
      return Value < (1ULL << 1);
    }
    return true;
  }

  bool isUImm2() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      uint64_t Value = CE->getValue();
      return Value < (1ULL << 2);
    }
    return true;
  }

  bool isUImm4() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      uint64_t Value = CE->getValue();
      return Value < (1ULL << 4);
    }
    return true;
  }

  // 7-bit signed immediate for the 16-bit MOVI pattern (§2.2, pattern 0110)
  // range -64..+63.
  bool isSImm7() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      int64_t Value = CE->getValue();
      return Value >= -(1LL << 6) && Value < (1LL << 6);
    }
    return true;
  }

  // 10-bit signed immediate for the 16-bit BRANCH pattern (§2.2, pattern 1000)
  // range +-512 words; the fixup scales to byte units.
  bool isSImm10() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      int64_t Value = CE->getValue();
      return Value >= -(1LL << 9) && Value < (1LL << 9);
    }
    return true;
  }

  // 7-bit unsigned immediate (zero-extended MOVI form).
  bool isUImm7() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      uint64_t Value = CE->getValue();
      return Value < (1ULL << 7);
    }
    return true;
  }

  // 10-bit unsigned immediate (reserved for future 16-bit ops).
  bool isUImm10() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      uint64_t Value = CE->getValue();
      return Value < (1ULL << 10);
    }
    return true;
  }

  bool isUImm5() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      uint64_t Value = CE->getValue();
      return Value < (1ULL << 5);
    }
    return true;
  }

  bool isUImm6() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      uint64_t Value = CE->getValue();
      return Value < (1ULL << 6);
    }
    return true;
  }

  bool isUImm8() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      uint64_t Value = CE->getValue();
      return Value < (1ULL << 8);
    }
    return true;
  }

  bool isUImm12() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      uint64_t Value = CE->getValue();
      return Value < (1ULL << 12);
    }
    return true;
  }

  bool isUImm16() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      uint64_t Value = CE->getValue();
      return Value < (1ULL << 16);
    }
    return true;
  }

  bool isUImm20() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      uint64_t Value = CE->getValue();
      return Value < (1ULL << 20);
    }
    return true;
  }

  // Add the operand to an instruction
  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCExpr *Expr = getImm();
    addExpr(Inst, Expr);
  }

  void addMemOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of memory operands!");
    Inst.addOperand(MCOperand::createReg(getMemBase()));
    addExpr(Inst, getMemOffset());
  }

  static std::unique_ptr<HaydnOperand> CreateToken(StringRef Str, SMLoc S) {
    return std::make_unique<HaydnOperand>(Str, S);
  }

  static std::unique_ptr<HaydnOperand> CreateReg(MCRegister RegVal, SMLoc S,
                                                   SMLoc E) {
    return std::make_unique<HaydnOperand>(RegVal, S, E);
  }

  static std::unique_ptr<HaydnOperand> CreateImm(const MCExpr *Val, SMLoc S,
                                                   SMLoc E) {
    return std::make_unique<HaydnOperand>(Val, S, E);
  }

  static std::unique_ptr<HaydnOperand> CreateMem(MCRegister Base,
                                                  const MCExpr *Offset,
                                                  SMLoc S, SMLoc E) {
    return std::make_unique<HaydnOperand>(Base, Offset, S, E);
  }

private:
  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const auto *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }
};

class HaydnAsmParser : public MCTargetAsmParser {
  // This tracks the parsing of operands during parseInstruction
  bool parseOperand(OperandVector &Operands);

  // Parse a register name
  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;

  // Try to parse a register (for expression parsing)
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                SMLoc &EndLoc) override;

  // Parse a memory operand (base+offset or [base, offset])
  ParseStatus parseMemoryOperand(OperandVector &Operands);

  // Parse an immediate expression
  bool parseImmediate(OperandVector &Operands);

  // Match and emit instruction
  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  // Parse instruction
  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  // \returns the logical base opcode for a `_S<k>` format-member \p Opc, or
  // \p Opc itself when it is not a member. The `_S0`/`_S1`/`_S2` members of a
  // multi-slot logical all share one AsmString, so the matcher always resolves
  // a bundle mnemonic to the FIRST member (`_S0`). De-materializing back to the
  // logical lets the textual slot position choose the member instead — see the
  // positional pack in parseInstruction.
  unsigned getLogicalBaseOpcode(unsigned Opc);

  // Parse directive
  ParseStatus parseDirective(AsmToken ID) override;

  // Accept { and } as valid statement-start tokens for VLIW bundle syntax
  bool tokenIsStartOfStatement(AsmToken::TokenKind Token) override {
    return Token == AsmToken::LCurly || Token == AsmToken::RCurly;
  }

  // Diagnostic types for operand validation (must be public for generated code)
public:
  enum HaydnMatchResultTy {
    Match_InvalidSImm16 = FIRST_TARGET_MATCH_RESULT_TY,
    Match_InvalidSImm20,
    Match_InvalidSImm32,
    Match_InvalidSImm6,
    Match_InvalidSImm8,
    Match_InvalidSImm12,
    Match_InvalidUImm12,
    Match_InvalidUImm16,
    Match_InvalidUImm20,
    Match_InvalidUImm1,
    Match_InvalidUImm2,
    Match_InvalidUImm4,
    Match_InvalidUImm5,
    Match_InvalidUImm6,
    Match_InvalidUImm8,
    Match_InvalidSImm7,
    Match_InvalidSImm10,
    Match_InvalidUImm7,
    Match_InvalidUImm10,
  };

  HaydnAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                 const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII) {
    // Initialize available features
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }

private:
  // Format-member opcode -> logical base, inverted from the generated
  // PlacementAlternative table on first use (getLogicalBaseOpcode).
  DenseMap<unsigned, unsigned> MemberToLogical;
  bool MemberToLogicalBuilt = false;

  // Auto-generated instruction matching functions
#define GET_ASSEMBLER_HEADER
#include "HaydnGenAsmMatcher.inc"
};

unsigned HaydnAsmParser::getLogicalBaseOpcode(unsigned Opc) {
  if (!MemberToLogicalBuilt) {
    MemberToLogicalBuilt = true;
    HaydnMCFormats Fmts;
    for (unsigned Logical = 0, E = MII.getNumOpcodes(); Logical != E; ++Logical)
      if (const std::vector<unsigned> *Alts =
              Fmts.getAlternateInstsOpcode(Logical))
        for (unsigned Member : *Alts)
          if (Member != 0 && Member != Logical)
            MemberToLogical.try_emplace(Member, Logical);
  }
  auto It = MemberToLogical.find(Opc);
  return It == MemberToLogical.end() ? Opc : It->second;
}

// Include the implementation of the auto-generated functions
#define GET_MATCHER_IMPLEMENTATION
#include "HaydnGenAsmMatcher.inc"

} // end anonymous namespace

bool HaydnAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                   SMLoc &EndLoc) {
  StartLoc = getParser().getTok().getLoc();

  if (!getParser().getTok().is(AsmToken::Identifier)) {
    return true;
  }

  StringRef Name = getParser().getTok().getString();

  // Try to match register name (case-insensitive)
  Reg = MatchRegisterName(Name);
  if (!Reg)
    Reg = MatchRegisterName(Name.lower());
  if (!Reg)
    Reg = MatchRegisterName(Name.upper());

  if (Reg) {
    EndLoc = getParser().getTok().getEndLoc();
    getParser().Lex(); // Eat register token
    return false;
  }

  return true; // Not a register
}

ParseStatus HaydnAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                              SMLoc &EndLoc) {
  // Try to parse but don't consume token if not a register
  StartLoc = getParser().getTok().getLoc();

  if (!getParser().getTok().is(AsmToken::Identifier)) {
    return ParseStatus::NoMatch;
  }

  StringRef Name = getParser().getTok().getString();

  Reg = MatchRegisterName(Name);
  if (!Reg)
    Reg = MatchRegisterName(Name.lower());
  if (!Reg)
    Reg = MatchRegisterName(Name.upper());

  if (Reg) {
    EndLoc = getParser().getTok().getEndLoc();
    getParser().Lex(); // Eat register token
    return ParseStatus::Success;
  }

  return ParseStatus::NoMatch;
}

bool HaydnAsmParser::parseImmediate(OperandVector &Operands) {
  SMLoc StartLoc = getParser().getTok().getLoc();
  const MCExpr *Expr;

  if (getParser().parseExpression(Expr)) {
    return Error(StartLoc, "expected immediate expression");
  }

  SMLoc EndLoc = SMLoc::getFromPointer(
      getParser().getTok().getLoc().getPointer() - 1);
  Operands.push_back(HaydnOperand::CreateImm(Expr, StartLoc, EndLoc));
  return false;
}

ParseStatus HaydnAsmParser::parseMemoryOperand(OperandVector &Operands) {
  SMLoc StartLoc = getParser().getTok().getLoc();
  MCRegister Base;
  SMLoc RegStart, RegEnd;

  // Check if this is a memory operand
  // Format 1: [Rs, offset]
  // Format 2: Rs, offset (same as register + immediate)

  bool HasBracket = false;
  if (getParser().getTok().is(AsmToken::LBrac)) {
    HasBracket = true;
    getParser().Lex(); // Eat '['
  }

  // Parse base register
  if (parseRegister(Base, RegStart, RegEnd)) {
    return ParseStatus::Failure;
  }

  const MCExpr *Offset = nullptr;
  SMLoc EndLoc;

  if (getParser().getTok().is(AsmToken::Comma)) {
    getParser().Lex(); // Eat ','
    if (getParser().parseExpression(Offset)) {
      return Error(getParser().getTok().getLoc(), "expected offset expression");
    }
  } else {
    // No offset, use 0
    Offset = MCConstantExpr::create(0, getContext());
  }

  if (HasBracket) {
    if (!getParser().getTok().is(AsmToken::RBrac)) {
      return Error(getParser().getTok().getLoc(), "expected ']'");
    }
    getParser().Lex(); // Eat ']'
    EndLoc = getParser().getTok().getLoc();
  } else {
    EndLoc = SMLoc::getFromPointer(
        getParser().getTok().getLoc().getPointer() - 1);
  }

  Operands.push_back(
      HaydnOperand::CreateMem(Base, Offset, StartLoc, EndLoc));
  return ParseStatus::Success;
}

bool HaydnAsmParser::parseOperand(OperandVector &Operands) {
  SMLoc StartLoc = getParser().getTok().getLoc();
  SMLoc EndLoc;

  switch (getParser().getTok().getKind()) {
  case AsmToken::LBrac:
    // Memory operand: [Rs, offset]
    if (parseMemoryOperand(Operands).isFailure()) {
      return true;
    }
    return false;

  case AsmToken::Identifier: {
    // Could be a register or an expression
    MCRegister Reg;
    if (!tryParseRegister(Reg, StartLoc, EndLoc).isNoMatch()) {
      // Successfully parsed as register
      Operands.push_back(HaydnOperand::CreateReg(Reg, StartLoc, EndLoc));
      return false;
    }
    // Fall through to expression parsing
    [[fallthrough]];
  }

  case AsmToken::Integer:
  case AsmToken::Plus:
  case AsmToken::Minus:
  case AsmToken::LParen:
    // Immediate expression
    return parseImmediate(Operands);

  default:
    return Error(StartLoc, "unexpected token in operand");
  }
}

bool HaydnAsmParser::parseInstruction(ParseInstructionInfo &Info,
                                       StringRef Name, SMLoc NameLoc,
                                       OperandVector &Operands) {
  MCAsmParser &Parser = getParser();

  // Handle standalone '}' — end of a VLIW bundle line (no-op statement).
  if (Name == "}") {
    while (Parser.getTok().isNot(AsmToken::EndOfStatement))
      Parser.Lex();
    Parser.Lex(); // Eat EndOfStatement
    Operands.push_back(HaydnOperand::CreateToken(Name, NameLoc));
    return false;
  }

  // Handle VLIW bundle syntax: { instr [; instr...] }
  // When '{' is the statement-start token, the generic parser consumed it and
  // passed Name="{" to us. The actual instruction mnemonic follows.
  bool InBundle = (Name == "{");

  if (InBundle) {
    // Skip whitespace after '{'
    while (Parser.getTok().is(AsmToken::Space))
      Parser.Lex();

    // AIE AsmParser shape (AIEBaseAsmParser.h:164-211):
    //   processMatchedInstruction: Bundle.canAdd(Inst) ? Bundle.add : Error
    //     "incorrect bundle"
    //   emitBundle: getFormatOrNull → for each slot emit child or NOP
    //     → B.setOpcode(Format->Opcode)
    // Product sole live Format->Opcode is BUNDLE128_FULL. Placement is Bundle
    // SlotMap (fixed getSlotKind / tryAdd alts); no Flags placement writers.
    //
    // Explicit `nop` in `{ nop; nop; op }` is a slot filler (residual
    // encodeBundle filtered NOP before pack). emitBundle pads empty slots —
    // do not canAdd/add NOP as a co-issue resource (NOP is S0-only alt). A
    // filler still HOLDS a textual slot position for the entries around it.
    //
    // Bundle text is written HIGH slot first and right-aligned on s0 — the ISA
    // spelling (VLIW_Engine_Compiler_Constraints.md "Bundle format:
    // G:{`slot2`, `slot1`, `slot0`}"). For N entries, entry i names slot
    // N-1-i: `{ a; b; c }` is s2,s1,s0; `{ a; b }` is s1,s0; `{ a }` is s0.
    //
    // That position is the slot HINT (Bundle::add(I*, MCSlotKind)), which is
    // what makes `clang -S` + reassemble reproduce the bytes of a direct
    // `clang -c` — the printer emits the same layout (BUNDLE128_FULL
    // AsmString "$s2; $s1; $s0"). An illegal hint falls back to solver
    // pickSlot, keeping short hand-written forms working.
    HaydnMCFormats Fmts;
    // Matched real children (NOP fillers excluded) with the textual slot index
    // they appeared at, before Bundle pack.
    SmallVector<std::pair<MCInst *, unsigned>, Haydn::ISSUE_SLOT_COUNT>
        RealChildren;
    unsigned TextSlot = 0;

    while (true) {
      // The next token should be the instruction mnemonic
      if (!Parser.getTok().is(AsmToken::Identifier)) {
        return Error(Parser.getTok().getLoc(),
                     "expected instruction mnemonic in bundle");
      }
      StringRef Mnemonic = Parser.getTok().getString();
      SMLoc MnemonicLoc = Parser.getTok().getLoc();
      Parser.Lex(); // Eat the mnemonic

      Operands.push_back(HaydnOperand::CreateToken(Mnemonic, MnemonicLoc));

      // Parse operands until EndOfStatement (from ';') or RCurly ('}')
      while (true) {
        if (Parser.getTok().is(AsmToken::EndOfStatement) ||
            Parser.getTok().is(AsmToken::RCurly)) {
          break;
        }
        // Comma between operands
        if (Operands.size() > 1) {
          if (Parser.getTok().is(AsmToken::Comma)) {
            Parser.Lex(); // Eat ','
          } else {
            return Error(Parser.getTok().getLoc(),
                         "expected comma between operands");
          }
        }
        if (parseOperand(Operands))
          return true;
      }

      // Match this instruction's operands to an MCInst. Allocate the MCInst
      // via MCContext so its lifetime extends through emission and any later
      // disassembly/printing pass that holds a pointer to the child (matches
      // the AsmPrinter/Disassembler pattern).
      MCInst *Child = Parser.getContext().createMCInst();
      uint64_t ChildErrorInfo = 0;
      unsigned MatchResult =
          MatchInstructionImpl(Operands, *Child, ChildErrorInfo, false);
      if (MatchResult != Match_Success) {
        SMLoc ErrLoc =
            (Operands.size() > 0) ? Operands[0]->getStartLoc() : NameLoc;
        return Error(ErrLoc, "failed to match instruction in bundle");
      }
      Child->setLoc(MnemonicLoc);

      // NOP is emit-time slot padding, not a co-issue resource — but it does
      // hold a textual slot position for the children that follow it.
      if (Child->getOpcode() != Haydn::NOP)
        RealChildren.push_back({Child, TextSlot});
      ++TextSlot;

      Operands.clear();

      // Check what terminated this instruction
      if (Parser.getTok().is(AsmToken::RCurly)) {
        Parser.Lex(); // Eat '}'
        break;        // End of bundle
      }
      if (Parser.getTok().is(AsmToken::EndOfStatement)) {
        // ';' produced EndOfStatement — check if there's another instruction
        // before '}' by peeking past whitespace.
        Parser.Lex(); // Eat EndOfStatement (';')
        // Skip whitespace
        while (Parser.getTok().is(AsmToken::Space))
          Parser.Lex();
        // If next is '}' or real EndOfStatement, we're done
        if (Parser.getTok().is(AsmToken::RCurly)) {
          Parser.Lex(); // Eat '}'
          break;
        }
        if (Parser.getTok().is(AsmToken::EndOfStatement)) {
          break; // Real end of line
        }
        // Otherwise there's another instruction — continue loop
        continue;
      }
      break;
    }

    // Consume any remaining EndOfStatement
    if (Parser.getTok().is(AsmToken::EndOfStatement))
      Parser.Lex();

    // A bundle written as a SINGLE entry carries no positional information:
    // `{ op }` is what the printer emits for an instruction that was never
    // bundled, whose slot the encoder picks. Keep the matched opcode there so
    // it re-encodes exactly as the `-c` path does (its right-aligned slot is
    // s0 either way). Two or three entries (the printer/objdump
    // `{ s2; s1; s0 }` layout, nop fillers included) DO name slots
    // positionally. An over-full bundle has no valid layout — leave every
    // child to the solver so canAdd reports the real conflict.
    const unsigned NumEntries = TextSlot;
    const bool Positional =
        NumEntries > 1 && NumEntries <= Haydn::ISSUE_SLOT_COUNT;

    // AIEBaseAsmParser.h:192-201 — Bundle.canAdd/add fail-closed.
    // Placement authority is Bundle SlotMap (textual hint, else tryAdd alts).
    //
    // For the positional layout, de-materialize a matched `_S<k>` member back
    // to its logical base first. All members of a multi-slot logical share one
    // AsmString, so the matcher pins the mnemonic to the FIRST member; that
    // member's fixed getSlotKind would reject any other hint (HaydnBundle.h
    // isHintSlotLegal) and force e.g. a slot-2 store back into slot 0. The
    // logical carries the full PlacementAlternative set, and encodeSlotSubInst
    // re-materializes Alts[SlotIdx] from the composite operand index — so the
    // slot the text names is the slot that gets encoded.
    Haydn::MCBundle Bundle(&Fmts);
    for (auto [Child, Index] : RealChildren) {
      if (Positional)
        Child->setOpcode(getLogicalBaseOpcode(Child->getOpcode()));
      if (!Bundle.canAdd(Child))
        return Error(Child->getLoc(), "incorrect bundle");
      if (Positional)
        Bundle.add(Child, MCSlotKind(MCSlotKind::Haydn_SLOT_S0 +
                                     static_cast<int>(NumEntries - 1 - Index)));
      else if (NumEntries == 1)
        Bundle.add(Child, MCSlotKind(MCSlotKind::Haydn_SLOT_S0));
      else
        Bundle.add(Child);
    }

    // emitBundle peer (AIEBaseAsmParser.h:164-181; HaydnAsmPrinter.cpp:481-527).
    // Empty RealChildren = pure stall (all explicit nops) — OccupiedSlots==0 is
    // covered by product FormatID BUNDLE128_FULL. Fail closed on standalone
    // unsupported / missing format.
    if (Bundle.isStandalone()) {
      Bundle.clear();
      return Error(NameLoc, "incorrect bundle");
    }
    const VLIWFormat *Format = Bundle.getFormatOrNull();
    if (!Format) {
      Bundle.clear();
      return Error(NameLoc, "incorrect bundle");
    }
    assert(Format->Opcode == Haydn::BUNDLE128_FULL &&
           "product live format must be BUNDLE128_FULL");

    MCInst MCB;
    MCB.setOpcode(Format->Opcode);
    for (unsigned K = 0; K < Haydn::ISSUE_SLOT_COUNT; ++K) {
      MCSlotKind Slot =
          MCSlotKind(MCSlotKind::Haydn_SLOT_S0 + static_cast<int>(K));
      MCInst *Instr = Bundle.at(Slot);
      if (!Instr) {
        Instr = Parser.getContext().createMCInst();
        unsigned NopOpc = Haydn::NOP;
        if (const MCSlotInfo *SI = Fmts.getSlotInfo(Slot)) {
          unsigned TableNop = SI->getNOPOpcode();
          if (TableNop != 0)
            NopOpc = TableNop;
        }
        Instr->setOpcode(NopOpc);
      }
      MCB.addOperand(MCOperand::createInst(Instr));
    }
    Parser.getStreamer().emitInstruction(MCB, getSTI());
    Bundle.clear();

    // Dummy token so matchAndEmitInstruction skips re-emit (already emitted).
    Operands.push_back(HaydnOperand::CreateToken("__bundle_emitted", NameLoc));
    return false;
  }

  // Normal (non-bundle) instruction parsing
  Operands.push_back(HaydnOperand::CreateToken(Name, NameLoc));

  while (Parser.getTok().isNot(AsmToken::EndOfStatement)) {
    if (Operands.size() > 1) {
      if (Parser.getTok().is(AsmToken::Comma)) {
        Parser.Lex(); // Eat ','
      } else {
        return Error(Parser.getTok().getLoc(),
                     "expected comma between operands");
      }
    }
    if (parseOperand(Operands))
      return true;
  }

  Parser.Lex(); // Eat EndOfStatement
  return false;
}

bool HaydnAsmParser::matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                              OperandVector &Operands,
                                              MCStreamer &Out,
                                              uint64_t &ErrorInfo,
                                              bool MatchingInlineAsm) {
  // Handle standalone '}' — end of VLIW bundle, no instruction to emit.
  // Also handle '__bundle_emitted' — instructions were already emitted in
  // parseInstruction for multi-instruction bundles.
  if (Operands.size() == 1 && Operands[0]->isToken()) {
    StringRef Tok = static_cast<HaydnOperand *>(Operands[0].get())->getToken();
    if (Tok == "}" || Tok == "__bundle_emitted")
      return false;
  }

  MCInst Inst;
  unsigned MatchResult =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);

  switch (MatchResult) {
  case Match_Success:
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, getSTI());
    return false;

  case Match_MissingFeature:
    return Error(IDLoc,
                 "instruction requires a CPU feature not currently enabled");

  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0ULL && ErrorInfo < Operands.size()) {
      ErrorLoc = Operands[ErrorInfo]->getStartLoc();
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }

  case Match_MnemonicFail:
    return Error(IDLoc, "invalid instruction mnemonic");

  default:
    return Error(IDLoc, "unknown error matching instruction");
  }
}

ParseStatus HaydnAsmParser::parseDirective(AsmToken ID) {
  return ParseStatus::NoMatch;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHaydnAsmParser() {
  RegisterMCAsmParser<HaydnAsmParser> X(getTheHaydnTarget());
}
