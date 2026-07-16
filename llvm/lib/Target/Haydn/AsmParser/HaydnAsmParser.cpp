//===---- HaydnAsmParser.cpp - Parse Haydn assembly to MCInst instructions --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "TargetInfo/HaydnTargetInfo.h"
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
  // Auto-generated instruction matching functions
#define GET_ASSEMBLER_HEADER
#include "HaydnGenAsmMatcher.inc"
};

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

    // Parse one or more instructions within the bundle, then form ONE
    // Haydn::BUNDLE MCInst whose operands are the per-slot child MCInsts
    // (allocated via MCContext for stable lifetime). The MC layer (InstPrinter
    // CodeEmitter, Disassembler) handles the bundle as a unit: encodeBundle
    // packs the children into a single 128-bit Bundle128 word, and printInst
    // renders them back as `{ op0; op1; op2 }`. Mirrors how HaydnAsmPrinter
    // (HaydnAsmPrinter.cpp:345) and HaydnDisassembler (HaydnDisassembler.cpp
    // 1204) build BUNDLE MCInsts: setOpcode(Haydn::BUNDLE) + one
    // MCOperand::createInst(child) per real child.
    //
    // Instructions are separated by ';' (which the lexer emits as
    // EndOfStatement) and the bundle ends with '}'.
    SmallVector<MCInst *, Haydn::ISSUE_SLOT_COUNT> BundleChildren;

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

      // Spec §1: a bundle holds at most ISSUE_SLOT_COUNT (3) ops. Reject a 4th
      // rather than silently dropping it — the encoder assumes ≤3 children.
      if (BundleChildren.size() == Haydn::ISSUE_SLOT_COUNT) {
        return Error(MnemonicLoc,
                     "bundle exceeds " +
                         Twine(Haydn::ISSUE_SLOT_COUNT) +
                         " issue slots");
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
      // Stage (b.1) — record the slot from source-order position. Inside a
      // `{ op0; op1; op2 }` bundle the Nth child occupies slot N (0=S0
      // 1=S1, 2=S2), matching the spec's left-to-right slot convention and
      // the encoder's operand-index→slot mapping (HaydnMCCodeEmitter
      // encodeSlotSubInst :1632-1647). The encoder does NOT read this yet
      // (stage b.2); purely additive, byte-identical.
      HaydnMCFlags::setHaydnSlot(*Child,
                                 static_cast<unsigned>(BundleChildren.size()));
      BundleChildren.push_back(Child);

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

    // Form ONE Haydn::BUNDLE MCInst with N MCOperand::createInst children.
    // encodeBundle (HaydnMCCodeEmitter.cpp:1642) iterates these operands and
    // packs every real child into a single 128-bit Bundle128 parcel; printInst
    // (HaydnInstPrinter.cpp:35) renders them as `{ op0; op1; op2 }`. A 1-child
    // bundle is the single-op case — same MCInst shape, different fill.
    MCInst MCB;
    MCB.setOpcode(Haydn::BUNDLE);
    for (MCInst *Child : BundleChildren)
      MCB.addOperand(MCOperand::createInst(Child));
    Parser.getStreamer().emitInstruction(MCB, getSTI());

    // Add a dummy token so the caller (matchAndEmitInstruction) has something.
    // matchAndEmitInstruction will emit nothing for this — the real BUNDLE
    // MCInst was emitted above.
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
