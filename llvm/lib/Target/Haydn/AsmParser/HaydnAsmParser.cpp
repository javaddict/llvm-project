//===---- HaydnAsmParser.cpp - Parse Haydn assembly to MCInst instructions --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnBundle.h"
#include "HaydnFormatERecords.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCAsmInfo.h"
#include "MCTargetDesc/HaydnMCChecker.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "MCTargetDesc/HaydnRelocLayout.h"
#include "TargetInfo/HaydnTargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
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

  // 7-bit signed immediate. Range -64..+63.
  bool isSImm7() const {
    if (!isImm())
      return false;
    if (const auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
      int64_t Value = CE->getValue();
      return Value >= -(1LL << 6) && Value < (1LL << 6);
    }
    return true;
  }

  // 10-bit signed immediate. Range ±512; the fixup scales to byte units.
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
  SMLoc getLoc() const { return getParser().getTok().getLoc(); }

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

  /// Parse `%hi12/%lo20/%pc_lo20(expr)` into an MCSpecifierExpr immediate.
  bool parseOperandWithSpecifier(OperandVector &Operands);
  bool parseExprWithSpecifier(const MCExpr *&Res, SMLoc &E);

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

/// Generated members (`_E2_`/`_E3_`) and residual FieldSlots (`*_S<digits>`)
/// are not public match results. Do not recover a catalog logical from a
/// suffix (AIE MultiSlot alts, AIEMCFormats.h:376-379). ABS64-class holes
/// keep the unsuffixed matcher name; `abs64_s1` is refused, not peeled.
static bool isPrivatePlacementOpcode(StringRef Name) {
  return haydnIsGeneratedMemberName(Name) ||
         haydnIsResidualFieldSlotName(Name);
}

static bool isPrivatePlacementInst(unsigned Opcode, const MCInstrInfo &MII) {
  if (Opcode == Haydn::NOP)
    return false;
  return isPrivatePlacementOpcode(MII.getName(Opcode)) ||
         haydnFindFormatEMemberByOpcode(Opcode);
}

/// Composite opcode from generated Mode membership + MemberId assignment.
/// AIE emitBundle takes Format->Opcode from getFormatOrNull / OccupiedSlots
/// (AIEBundle.h:150-156, AIEBaseAsmParser.h:164-180), not child cardinality.
/// Extra NOP pads are not occupancy. Count never invents a Format E row:
/// E2-only stays E2 (or fail); E3-only stays E3 (or fail); mixed Mode-only
/// fails; both-legal uses PacketFormats first-covering membership.
static unsigned selectParsedFormatEComposite(ArrayRef<unsigned> RealOpcs,
                                            const MCInstrInfo &MII) {
  for (unsigned Opc : RealOpcs) {
    if (Opc == 0 || Opc == Haydn::NOP)
      continue;
    if (isPrivatePlacementInst(Opc, MII))
      return 0;
  }
  // Same first-covering membership as standalone MC
  // (AIEBaseAsmParser.h:164-180). Three dual-mode logicals that place in
  // E2 entries must not select the two-entry row and then fail as
  // "incorrect bundle".
  return haydnSelectStandaloneFormatEOpcode(RealOpcs);
}

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

  case AsmToken::Percent:
    return parseOperandWithSpecifier(Operands);

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

bool HaydnAsmParser::parseOperandWithSpecifier(OperandVector &Operands) {
  SMLoc S = getParser().getTok().getLoc();
  SMLoc E;
  if (parseToken(AsmToken::Percent, "expected '%' relocation specifier"))
    return true;
  const MCExpr *Expr = nullptr;
  if (parseExprWithSpecifier(Expr, E))
    return true;
  Operands.push_back(HaydnOperand::CreateImm(Expr, S, E));
  return false;
}

bool HaydnAsmParser::parseExprWithSpecifier(const MCExpr *&Res, SMLoc &E) {
  SMLoc Loc = getParser().getTok().getLoc();
  if (!getParser().getTok().is(AsmToken::Identifier))
    return Error(Loc, "expected '%' relocation specifier");
  StringRef Identifier = getParser().getTok().getIdentifier();
  Haydn::Specifier Spec = Haydn::parseSpecifierName(Identifier);
  if (!Spec)
    return Error(Loc, "invalid relocation specifier");

  getParser().Lex();
  if (parseToken(AsmToken::LParen, "expected '('"))
    return true;

  const MCExpr *SubExpr = nullptr;
  if (getParser().parseParenExpression(SubExpr, E))
    return true;

  // Constant specifier payload uses RelocFieldInfo FieldSize / Trans
  // (same computeRelocValue as applyFixup). Symbols stay symbolic.
  int64_t Abs = 0;
  if (SubExpr->evaluateAsAbsolute(Abs)) {
    const auto Kind = static_cast<HaydnReloc::RelocKind>(Spec);
    HaydnReloc::RelocCompute Comp =
        HaydnReloc::computeRelocValue(Kind, static_cast<uint64_t>(Abs));
    if (!Comp.OK)
      return Error(Loc, Comp.Err ? Comp.Err
                                 : "relocation specifier immediate out of "
                                   "generated field range");
  }

  Res = MCSpecifierExpr::create(SubExpr, Spec, getContext(), Loc);
  return false;
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

    // Hand-assembly placement owner for braced Format E bundles.
    // Product profile is Format E (registry EncodedBytes). Explicit `nop` is
    // entry fill, not a co-issue resource — but it HOLDS a textual entry
    // position for the real ops around it (CB-142 / #10).
    //
    // Bundle text is HIGH entry first, right-aligned on e0 (AsmString
    // `$e2; $e1; $e0` / `$e1; $e0`). For N textual entries, entry i names
    // encode-dag position N-1-i: `{ a; b; c }` → e2,e1,e0; `{ a; b }` →
    // e1,e0; `{ a }` is single-entry (encoder/e0 placement, no reverse).
    // Text children include explicit nop fillers (catalog `{ insn; nop; nop }`).
    SmallVector<MCInst *, 4> TextChildren;

    while (true) {
      // The next token should be the instruction mnemonic
      if (!Parser.getTok().is(AsmToken::Identifier)) {
        return Error(Parser.getTok().getLoc(),
                     "expected instruction mnemonic in bundle");
      }
      StringRef Mnemonic = Parser.getTok().getString();
      SMLoc MnemonicLoc = Parser.getTok().getLoc();
      Parser.Lex(); // Eat the mnemonic

      // Refuse generated members / residual FieldSlots before match.
      // Do not peel `_S*` into a catalog logical (AIEMCFormats.h:376-379).
      if (isPrivatePlacementOpcode(Mnemonic))
        return Error(MnemonicLoc,
                     "assembler matched a private placement opcode");

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

      // NOP is emit-time entry padding, not a co-issue resource. Extra
      // textual NOPs are idle fill, not occupancy and not a third entry.
      TextChildren.push_back(Child);

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

    // All-NOP text is the generated E2 idle cycle (NOP in every E2 entry).
    // Extra textual NOPs are idle fill, not a third entry and never row
    // identity. Drop leading/trailing NOPs to the E3 bound first; leftover
    // overflow is too many real members. Reject `{ }`.
    auto dropIdleNopsToFit = [](SmallVectorImpl<MCInst *> &Kids,
                                unsigned Cap) {
      while (Kids.size() > Cap) {
        if (Kids.back()->getOpcode() == Haydn::NOP) {
          Kids.pop_back();
          continue;
        }
        if (Kids.front()->getOpcode() == Haydn::NOP) {
          Kids.erase(Kids.begin());
          continue;
        }
        break;
      }
    };
    unsigned MaxEntries = 0;
    for (const haydn::format::BundleFormatRowDesc &R :
         haydn::format::getProductBundleFormatRows())
      if (R.EntryCount > MaxEntries)
        MaxEntries = R.EntryCount;
    if (TextChildren.empty())
      return Error(NameLoc, "empty bundle");
    if (MaxEntries == 0)
      return Error(NameLoc, "incorrect bundle");
    dropIdleNopsToFit(TextChildren, MaxEntries);
    if (TextChildren.size() > MaxEntries)
      return Error(NameLoc, "Format E bundle supports at most three entries");

    // Standalone row from generated membership / unit occupancy, never
    // text-slot or real-child cardinality. Extra NOP pads hold entry
    // positions only inside the membership row.
    SmallVector<const MCInst *, 3> RealPtrs;
    SmallVector<unsigned, 3> RealOpcs;
    bool AnyE3Only = false;
    bool AnyE2Only = false;
    for (MCInst *Child : TextChildren) {
      // Refuse generated members / residual FieldSlots before Mode select.
      // Do not peel `_S*` into a catalog logical to pick E2 vs E3.
      if (isPrivatePlacementInst(Child->getOpcode(), MII))
        return Error(Child->getLoc(),
                     "assembler matched a private placement opcode");
      if (Child->getOpcode() == Haydn::NOP)
        continue;
      RealPtrs.push_back(Child);
      RealOpcs.push_back(Child->getOpcode());
      if (haydnFormatELogicalIsE3Only(Child->getOpcode()))
        AnyE3Only = true;
      if (haydnFormatELogicalIsE2Only(Child->getOpcode()))
        AnyE2Only = true;
    }
    if (AnyE2Only && AnyE3Only)
      return Error(NameLoc,
                   "incorrect bundle: mixed E2-only and E3-only logicals");
    const unsigned CompositeOpc =
        selectParsedFormatEComposite(RealOpcs, MII);
    if (!CompositeOpc) {
      if (AnyE2Only)
        return Error(NameLoc,
                     "incorrect bundle: E2-only logical cannot occupy "
                     "a three-entry row");
      return Error(NameLoc, "incorrect bundle");
    }
    const haydn::format::BundleFormatRowDesc *Row =
        haydnFormatERowForCompositeOpcode(CompositeOpc);
    if (!Row || Row->EntryCount == 0)
      return Error(NameLoc, "incorrect bundle");
    const unsigned EntryCount = Row->EntryCount;
    // Selected row capacity is a bound, not a reason to invent the other
    // Format E row from child count.
    if (RealPtrs.size() > EntryCount) {
      if (AnyE2Only)
        return Error(NameLoc,
                     "incorrect bundle: E2-only logical cannot occupy "
                     "a three-entry row");
      return Error(NameLoc, "incorrect bundle");
    }

    // Extra textual NOP pads past the membership row are idle fill, not a
    // third entry. Drop trailing then leading NOPs until the selected row
    // fits. Occupancy already chose the row — this is not count→E3.
    dropIdleNopsToFit(TextChildren, EntryCount);
    if (TextChildren.size() > EntryCount)
      return Error(NameLoc, "incorrect bundle");

    // Parse-time legality (Hexagon MCChecker): unit injectivity, WAW,
    // SET_HWLOOP sel, RF-port ceilings. Pass the membership row, not the
    // pre-compress text count, so E2-only `{ insn; nop; nop }` is not
    // forced onto E3 cover.
    if (auto Err = haydnCheckParsedBundle(
            RealPtrs, EntryCount, MII, Parser.getContext().getRegisterInfo()))
      return Error(NameLoc, "incorrect bundle: " + *Err);

    const unsigned PlaceN = TextChildren.size();
    const bool Positional = PlaceN > 1;
    SmallVector<MCInst *, 3> Entries(EntryCount, nullptr);
    for (unsigned I = 0; I < PlaceN; ++I) {
      MCInst *Child = TextChildren[I];
      if (Child->getOpcode() == Haydn::NOP)
        continue;
      unsigned EntryIdx = Positional ? PlaceN - 1 - I : 0;
      if (EntryIdx >= EntryCount)
        return Error(Child->getLoc(), "incorrect bundle");
      if (Entries[EntryIdx])
        return Error(Child->getLoc(), "incorrect bundle");
      Entries[EntryIdx] = Child;
    }

    MCInst MCB;
    MCB.setOpcode(CompositeOpc);
    for (unsigned E = 0; E < EntryCount; ++E) {
      MCInst *Instr = Entries[E];
      if (!Instr) {
        Instr = Parser.getContext().createMCInst();
        Instr->setOpcode(Haydn::NOP);
      }
      MCB.addOperand(MCOperand::createInst(Instr));
    }
    Parser.getStreamer().emitInstruction(MCB, getSTI());

    // Dummy token so matchAndEmitInstruction skips re-emit (already emitted).
    Operands.push_back(HaydnOperand::CreateToken("__bundle_emitted", NameLoc));
    return false;
  }

  // Normal (non-bundle) instruction parsing. Refuse private placement
  // mnemonics before MatchInstructionImpl so `_S*` peel is unreachable.
  if (isPrivatePlacementOpcode(Name))
    return Error(NameLoc, "assembler matched a private placement opcode");
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
    if (isPrivatePlacementInst(Inst.getOpcode(), MII))
      return Error(IDLoc, "assembler matched a private placement opcode");
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
    // Do not recover a catalog logical by stripping `_S*` / `_E2_` / `_E3_`.
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
