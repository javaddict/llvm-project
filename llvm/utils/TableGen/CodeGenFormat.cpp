//===- CodeGenFormat.cpp - Format Generator Generator
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Modifications (c) Copyright 2023-2025 Advanced Micro Devices, Inc. or its
// affiliates
//
//===----------------------------------------------------------------------===//

#include "CodeGenFormat.h"
#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenTarget.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/DirectiveEmitter.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <bitset>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <stack>
#include <string>
#include <utility>
#include <optional>
#include <vector>

using namespace llvm;

// Forward declarations: helpers defined after run() and called from it.
// GET_ALTERNATE_INST_OPCODE_FUNC order:
//   1) MultiSlot_Pseudo.materializableInto (AIE-shaped)
//   2) LogicalMaterialize.materializableInto (bulk declarative typed relation)
// Residual `_S{0,1,2}` name discovery is not a product alternate source: any
// setDesc-safe sparse member row without an explicit list fails TableGen.
struct SlotMemberVariantRow {
  unsigned Members[3] = {0, 0, 0};
};
static std::map<unsigned, SlotMemberVariantRow>
collectSparseAltSlotMemberRows(
    ArrayRef<const CodeGenInstruction *> NumberedInstructions);
static std::map<unsigned, SlotMemberVariantRow>
collectLogicalMaterializeRows(
    ArrayRef<const CodeGenInstruction *> NumberedInstructions,
    const RecordKeeper &Records);
static void emitAlternateInstsOpcodeFunc(
    raw_ostream &o, const CodeGenTarget &Target,
    ArrayRef<const CodeGenInstruction *> NumberedInstructions,
    const std::vector<TGInstrLayout> &PseudoInstFormats,
    const RecordKeeper &Records);

//===----------------------------------------------------------------------===//
// VF1 setDesc logical→member compatibility (durable-rule 8e / plan §3.1 §8.4.2)
//
// MI.setDesc swaps only the descriptor. Operand vector, ties, implicits,
// MMOs, semantic flags, and sched timing are NOT rebuilt. CodeGenFormat must
// therefore reject any AlternateInsts / sparse `_S*` pair that is unsafe for
// unconditional post-RA setDesc. Materialize stays AIE-unconditional once
// generation has proven shape.
//===----------------------------------------------------------------------===//

namespace {

/// Itinerary dependency latency + uop count keyed by InstrItinClass.
struct ItinTimingInfo {
  std::vector<int64_t> OperandCycles;
  int64_t NumMicroOps = 1;
  bool Valid = false;
};

static DenseMap<const Record *, ItinTimingInfo>
buildItinTimingByClass(const RecordKeeper &Records) {
  DenseMap<const Record *, ItinTimingInfo> Map;
  for (const Record *D : Records.getAllDerivedDefinitions("InstrItinData")) {
    if (!D->getValue("TheClass") || !D->getValue("OperandCycles"))
      continue;
    const Record *Class = D->getValueAsDef("TheClass");
    ItinTimingInfo T;
    T.OperandCycles = D->getValueAsListOfInts("OperandCycles");
    if (D->getValue("NumMicroOps"))
      T.NumMicroOps = D->getValueAsInt("NumMicroOps");
    T.Valid = true;
    // Single product model: last writer wins if multiple processors define
    // the same class (Haydn has one itinerary table).
    Map[Class] = std::move(T);
  }
  return Map;
}

static const ItinTimingInfo *
lookupItinTiming(const DenseMap<const Record *, ItinTimingInfo> &Map,
                 const CodeGenInstruction &I) {
  if (!I.TheDef->getValue("Itinerary"))
    return nullptr;
  const Record *Itin = I.TheDef->getValueAsDef("Itinerary");
  if (!Itin || Itin->getName() == "NoItinerary")
    return nullptr;
  auto It = Map.find(Itin);
  if (It == Map.end() || !It->second.Valid)
    return nullptr;
  return &It->second;
}

static void appendImplicitNames(const std::vector<const Record *> &Regs,
                                SmallVectorImpl<StringRef> &Out) {
  for (const Record *R : Regs)
    Out.push_back(R->getName());
  llvm::sort(Out);
}

/// Structural setDesc shape (L242/L254): operands/defs/ties/type-kind/regclass.
/// Empty => structurally safe for MI.setDesc without operand-vector rewrite.
static std::string
diagnoseSetDescStructural(const CodeGenInstruction &Logical,
                          const CodeGenInstruction &Member) {
  if (Logical.Operands.NumDefs != Member.Operands.NumDefs) {
    return ("NumDefs mismatch (logical " + Twine(Logical.Operands.NumDefs) +
            " vs member " + Twine(Member.Operands.NumDefs) + ")")
        .str();
  }
  if (Logical.Operands.size() != Member.Operands.size()) {
    return ("operand count mismatch (logical " + Twine(Logical.Operands.size()) +
            " vs member " + Twine(Member.Operands.size()) + ")")
        .str();
  }
  if (Logical.Operands.isVariadic != Member.Operands.isVariadic)
    return "variadic operand flag mismatch";

  for (unsigned OpIdx = 0, E = Logical.Operands.size(); OpIdx != E; ++OpIdx) {
    const CGIOperandList::OperandInfo &LO = Logical.Operands[OpIdx];
    const CGIOperandList::OperandInfo &MO = Member.Operands[OpIdx];
    if (LO.MINumOperands != MO.MINumOperands) {
      return ("operand " + Twine(OpIdx) + " MINumOperands mismatch (logical " +
              Twine(LO.MINumOperands) + " vs member " + Twine(MO.MINumOperands) +
              ")")
          .str();
    }
    if (LO.Constraints.size() != MO.Constraints.size()) {
      return ("operand " + Twine(OpIdx) + " constraint arity mismatch").str();
    }
    for (unsigned C = 0, CE = LO.Constraints.size(); C != CE; ++C) {
      if (LO.Constraints[C] != MO.Constraints[C]) {
        return ("operand " + Twine(OpIdx) + " sub-op " + Twine(C) +
                " tie/early-clobber constraint mismatch")
            .str();
      }
    }
    // Name differences ($rd vs $rt) are allowed — setDesc keeps the MI MO list.
    if (LO.Rec->getName() == MO.Rec->getName())
      continue;

    bool LReg = LO.Rec->isSubClassOf("RegisterClass") ||
                LO.Rec->isSubClassOf("RegisterOperand");
    bool MReg = MO.Rec->isSubClassOf("RegisterClass") ||
                MO.Rec->isSubClassOf("RegisterOperand");
    bool LOp = LO.Rec->isSubClassOf("Operand");
    bool MOp = MO.Rec->isSubClassOf("Operand");
    if (LReg != MReg || (LOp && !LReg) != (MOp && !MReg)) {
      return ("operand " + Twine(OpIdx) + " type-kind mismatch (" +
              LO.Rec->getName() + " vs " + MO.Rec->getName() + ")")
          .str();
    }
    if (LReg && MReg && LO.Rec != MO.Rec) {
      const Record *LClass = LO.Rec;
      const Record *MClass = MO.Rec;
      if (LO.Rec->isSubClassOf("RegisterOperand") && LO.Rec->getValue("RegClass"))
        LClass = LO.Rec->getValueAsDef("RegClass");
      if (MO.Rec->isSubClassOf("RegisterOperand") && MO.Rec->getValue("RegClass"))
        MClass = MO.Rec->getValueAsDef("RegClass");
      if (LClass != MClass) {
        return ("operand " + Twine(OpIdx) + " register-class mismatch (" +
                LO.Rec->getName() + " vs " + MO.Rec->getName() + ")")
            .str();
      }
    }
  }
  return {};
}

/// Full setDesc contract (durable-rule 8e / plan §3.1 §8.4.2): structural +
/// implicits + semantic flags + itinerary latency/uops coverage.
static std::string
diagnoseSetDescIncompatibility(const CodeGenInstruction &Logical,
                               const CodeGenInstruction &Member,
                               const DenseMap<const Record *, ItinTimingInfo>
                                   &ItinMap) {
  if (std::string Why = diagnoseSetDescStructural(Logical, Member); !Why.empty())
    return Why;

  // Implicit uses / defs (exact; setDesc does not rebuild implicits).
  {
    SmallVector<StringRef, 8> LUses, MUses, LDefs, MDefs;
    appendImplicitNames(Logical.ImplicitUses, LUses);
    appendImplicitNames(Member.ImplicitUses, MUses);
    appendImplicitNames(Logical.ImplicitDefs, LDefs);
    appendImplicitNames(Member.ImplicitDefs, MDefs);
    if (LUses != MUses)
      return "implicit Uses mismatch";
    if (LDefs != MDefs)
      return "implicit Defs mismatch";
  }

  // Semantic flags: member bits must be covered by logical. mayLoad/mayStore
  // are hard; hasSideEffects is required only when the member is not already
  // modeled as load/store/call/branch/terminator/barrier on the logical
  // (product stores set hasSideEffects=1 on members while logicals use
  // mayStore — setDesc may gain UnmodeledSideEffects after commit).
  auto flagCover = [](bool Log, bool Mem, StringRef Name,
                      std::string &Why) -> bool {
    if (Mem && !Log) {
      Why = ("member sets " + Name + " but logical does not").str();
      return false;
    }
    return true;
  };
  std::string FlagWhy;
  if (!flagCover(Logical.mayLoad, Member.mayLoad, "mayLoad", FlagWhy) ||
      !flagCover(Logical.mayStore, Member.mayStore, "mayStore", FlagWhy) ||
      !flagCover(Logical.isCall, Member.isCall, "isCall", FlagWhy) ||
      !flagCover(Logical.isReturn, Member.isReturn, "isReturn", FlagWhy) ||
      !flagCover(Logical.isBranch, Member.isBranch, "isBranch", FlagWhy) ||
      !flagCover(Logical.isIndirectBranch, Member.isIndirectBranch,
                 "isIndirectBranch", FlagWhy) ||
      !flagCover(Logical.isTerminator, Member.isTerminator, "isTerminator",
                 FlagWhy) ||
      !flagCover(Logical.isBarrier, Member.isBarrier, "isBarrier", FlagWhy) ||
      !flagCover(Logical.mayRaiseFPException, Member.mayRaiseFPException,
                 "mayRaiseFPException", FlagWhy) ||
      !flagCover(Logical.isConvergent, Member.isConvergent, "isConvergent",
                 FlagWhy) ||
      !flagCover(Logical.hasDelaySlot, Member.hasDelaySlot, "hasDelaySlot",
                 FlagWhy))
    return FlagWhy;

  if (Member.hasSideEffects && !Logical.hasSideEffects) {
    const bool Modeled =
        Logical.mayLoad || Logical.mayStore || Logical.isCall ||
        Logical.isBranch || Logical.isTerminator || Logical.isBarrier ||
        Logical.isReturn || Logical.isIndirectBranch;
    if (!Modeled)
      return "member sets hasSideEffects but logical has no modeled "
             "load/store/control/side-effect flag";
  }

  if (Member.mayLoad && Logical.mayLoad_Unset)
    return "logical mayLoad is unset while member mayLoad=1";
  if (Member.mayStore && Logical.mayStore_Unset)
    return "logical mayStore is unset while member mayStore=1";

  // Timing / uops: logical must equal or safely cover every member.
  const ItinTimingInfo *LT = lookupItinTiming(ItinMap, Logical);
  const ItinTimingInfo *MT = lookupItinTiming(ItinMap, Member);
  if (LT && MT) {
    if (LT->NumMicroOps < MT->NumMicroOps) {
      return ("NumMicroOps not covered (logical " + Twine(LT->NumMicroOps) +
              " < member " + Twine(MT->NumMicroOps) + ")")
          .str();
    }
    const unsigned N =
        std::max(LT->OperandCycles.size(), MT->OperandCycles.size());
    for (unsigned I = 0; I < N; ++I) {
      const int64_t LCyc =
          I < LT->OperandCycles.size() ? LT->OperandCycles[I] : 0;
      const int64_t MCyc =
          I < MT->OperandCycles.size() ? MT->OperandCycles[I] : 0;
      if (LCyc < MCyc) {
        return ("OperandCycles[" + Twine(I) + "] not covered (logical " +
                Twine(LCyc) + " < member " + Twine(MCyc) + ")")
            .str();
      }
    }
  }

  return {};
}

static void
requireSetDescFullyCompatible(const CodeGenInstruction &Logical,
                              const CodeGenInstruction &Member,
                              const DenseMap<const Record *, ItinTimingInfo>
                                  &ItinMap) {
  std::string Why = diagnoseSetDescIncompatibility(Logical, Member, ItinMap);
  if (Why.empty())
    return;
  PrintFatalError(Member.TheDef->getLoc(),
                  "setDesc-incompatible logical→member pair: " +
                      Logical.TheDef->getName() + " → " +
                      Member.TheDef->getName() + ": " + Why);
}

static StringRef stripTargetNamespace(StringRef Qualified) {
  // "Haydn::ADD32_S0" → "ADD32_S0"; bare names pass through.
  size_t Pos = Qualified.rfind(':');
  if (Pos == StringRef::npos)
    return Qualified;
  return Qualified.drop_front(Pos + 1);
}

/// VF1.3 setDesc gate:
/// - MultiSlot_Pseudo materializableInto: full shape/flag/sched/uops contract
///   (PrintFatalError). Explicit author list must be setDesc-safe.
/// - LogicalMaterialize members: structural setDesc filter at emit time;
///   uncovered setDesc-safe `_S*` rows fail closed (no suffix-discovery backfill).
static void validateSetDescAlternateCompatibility(
    ArrayRef<const CodeGenInstruction *> NumberedInstructions,
    const std::vector<TGInstrLayout> &PseudoInstFormats,
    const RecordKeeper &Records) {
  const auto ItinMap = buildItinTimingByClass(Records);

  StringMap<const CodeGenInstruction *> ByName;
  for (const CodeGenInstruction *CGI : NumberedInstructions)
    ByName[CGI->TheDef->getName()] = CGI;

  // Dense MultiSlot_Pseudo path — full durable-rule 8e contract.
  for (const TGInstrLayout &Pseudo : PseudoInstFormats) {
    auto LIt = ByName.find(Pseudo.getInstrName());
    if (LIt == ByName.end()) {
      PrintFatalError("MultiSlot_Pseudo '" + Pseudo.getInstrName() +
                      "' missing from numbered instructions");
    }
    const CodeGenInstruction &Logical = *LIt->second;
    for (const std::string &Alt : Pseudo.getAlternateInsts()) {
      StringRef MemName = stripTargetNamespace(Alt);
      auto MIt = ByName.find(MemName);
      if (MIt == ByName.end()) {
        PrintFatalError(Logical.TheDef->getLoc(),
                        "setDesc alternate '" + Alt + "' for logical '" +
                            Logical.TheDef->getName() +
                            "' not found among instructions");
      }
      requireSetDescFullyCompatible(Logical, *MIt->second, ItinMap);
    }
  }
}

/// Apply structural setDesc filtering to sparse rows: zero out members that
/// cannot be MI.setDesc'd without operand-vector rewrite.
static std::map<unsigned, SlotMemberVariantRow>
filterSparseAltsSetDescStructural(
    ArrayRef<const CodeGenInstruction *> NumberedInstructions,
    std::map<unsigned, SlotMemberVariantRow> Rows) {
  for (auto &KV : Rows) {
    const CodeGenInstruction &Logical = *NumberedInstructions[KV.first];
    SlotMemberVariantRow &Row = KV.second;
    for (unsigned Slot = 0; Slot < 3; ++Slot) {
      if (Row.Members[Slot] == 0)
        continue;
      const CodeGenInstruction &Member =
          *NumberedInstructions[Row.Members[Slot]];
      if (!diagnoseSetDescStructural(Logical, Member).empty())
        Row.Members[Slot] = 0; // reject unsafe sparse alternate
    }
  }
  return Rows;
}

} // end anonymous namespace

void CodeGenFormat::run(raw_ostream &o) {
  CodeGenTarget Target(Records);
  const std::string CurrentNamespace = Target.getName().str();

  std::vector<const Record *> CodeGenFormatRecords =
      Records.getAllDerivedDefinitions("CodeGenFormat");
  unsigned Size = CodeGenFormatRecords.size();

  if (Size > 1)
    errs() << "Invalid number of CodeGenFormat instantiations: "
           << "There must be only one Record deriving it.\n";
  else if (Size == 0)
    errs() << "Invalid number of CodeGenFormat instantiations: "
           << "The class CodeGenFormat must be derived once.\n";
  if (Size != 1)
    exit(EXIT_FAILURE);

  const std::string FormatClassEmitted("MCFormatDesc");

  TGTargetSlots Slots(CurrentNamespace, Records.getClass("InstSlot"));
  std::vector<const Record *> InstRecords =
      Records.getAllDerivedDefinitions("Instruction");
  std::vector<const Record *> SlotRecords =
      Records.getAllDerivedDefinitions("InstSlot");

  // Register all Slot based Record into the data structure
  for (const Record *Slot : SlotRecords)
    Slots.addSlot(Slot);
  // Finalizing the slots, preparing them for the emission
  Slots.finalizeSlots();

  // For little-endian instruction bit encodings, reverse the bit order
  Target.reverseBitsForLittleEndianEncoding();

  // Instructions are going to be ordered as they are in
  // CodeGenEmitter
  // NOTE (Haydn port): upstream renamed getInstructionsByEnumValue to
  // getInstructions (same ArrayRef<const CodeGenInstruction *> return).
  ArrayRef<const CodeGenInstruction *> NumberedInstructions =
      Target.getInstructions();

  // Our main container of Formats
  std::vector<TGInstrLayout> InstFormats;
  // Contains only multislot pseudo inst.
  std::vector<TGInstrLayout> PseudoInstFormats;

  for (const CodeGenInstruction *CGI : NumberedInstructions) {
    const Record *R = CGI->TheDef;

    if ((R->getValueAsString("Namespace") == "TargetOpcode") ||
        !R->isSubClassOf("InstFormat") ||
        (!R->getValue("Inst") && !R->getValue("isMultiSlotPseudo")))
      continue;

    // Build a tree of fields based on the encoding infos of the current
    // instruction
    TGInstrLayout IL(CGI, Slots);
    // For debugging purposes:
    // IL.dump(std::cout);
    InstFormats.push_back(IL);

    if (IL.hasMultipleSlotOptions())
      PseudoInstFormats.push_back(IL);
  }

  // Populate slot map for multislot pseudo inst.
  for (TGInstrLayout &PseudoInst : PseudoInstFormats)
    PseudoInst.addAlternateInstInMultiSlotPseudo(InstFormats);

  assert(Slots.size() != 0 && "no Slot detected");

  computeSlotSets(Slots, InstFormats);

  o << "#ifdef GET_FORMATS_SLOTKINDS\n"
       "#undef GET_FORMATS_SLOTKINDS\n\n";
  Slots.emitTargetSlotKindEnum(o);
  o << "#endif // GET_FORMATS_SLOTKINDS\n\n";

  o << "#ifdef GET_FORMATS_SLOTS_DEFS\n"
    << "#undef GET_FORMATS_SLOTS_DEFS\n\n";
  Slots.emitSlotsInfoInstantiation(o, TGInstrLayout::getNOPSlotMapper());
  o << "#endif // GET_FORMATS_SLOTS_DEFS\n\n";

  o << "#ifdef GET_FORMATS_SLOTINFOS_MAPPING\n"
       "#undef GET_FORMATS_SLOTINFOS_MAPPING\n";
  Slots.emitTargetSlotMapping(o);
  o << "#endif // GET_FORMATS_SLOTINFOS_MAPPING\n\n";

  o << "#ifdef GET_OPCODE_FORMATS_INDEX_FUNC\n"
    << "#undef GET_OPCODE_FORMATS_INDEX_FUNC\n";
  o << "std::optional<unsigned int> " << Target.getName().str()
    << "MCFormats::getFormatDescIndex";
  o << "(unsigned int Opcode) const {\n";
  o << "  switch (Opcode) {\n";
  o << "  default:\n";
  o << "    return std::nullopt;\n";
  for (const TGInstrLayout &Inst : InstFormats)
    Inst.emitOpcodeFormatIndex(o);
  o << "  }\n}\n";
  o << "#endif // GET_OPCODE_FORMATS_INDEX_FUNC\n\n";

  // ---------------------------------------------------------------------
  // GET_ALTERNATE_INST_OPCODE_FUNC (AIE MultiSlot_Pseudo + Haydn sparse alts)
  // ---------------------------------------------------------------------
  // AIE path: MultiSlot_Pseudo materializableInto via
  // addAlternateInstInMultiSlotPseudo (CodeGenFormat.cpp peer of AIE
  // CodeGenFormat.cpp:467-499 / emit 142-164).
  // Haydn: synthesize sparse size-3 AlternateInsts from Full-format
  // `_S0`/`_S1`/`_S2` slot members (vector index == field/slot; 0 for
  // missing). PlacementAlternative getLegalSlots ORs non-zero indices;
  // post-RA leaveRegion setDesc(member); MC encodes Desc as-is.
  // VF1.3: prove every logical→member alternate is setDesc-safe (operands/
  // ties/implicits/flags/sched/uops) before emission — durable-rule 8e.
  validateSetDescAlternateCompatibility(NumberedInstructions,
                                        PseudoInstFormats, Records);
  emitAlternateInstsOpcodeFunc(o, Target, NumberedInstructions,
                               PseudoInstFormats, Records);

  if (InstFormats.size() > 0 && Slots.size() > 0) {
    o << "#ifdef GET_FORMATS_FORMATS_DEFS\n"
      << "#undef GET_FORMATS_FORMATS_DEFS\n\n";

    ConstTable FieldsHierarchy("MCFormatField", "FieldsHierarchyTables");

    unsigned BaseIndex = 0;
    for (const TGInstrLayout &Inst : InstFormats)
      Inst.emitFlatTree(FieldsHierarchy, BaseIndex);
    FieldsHierarchy.finish();

    o << FieldsHierarchy;

    {
      ConstTable OpFields("const MCFormatField *", "OpFields");
      ConstTable FieldRanges("const MCFormatField * const *", "FieldRanges");
      ConstTable SlotsFields("SlotFieldPair", "SlotsFields");
      ConstTable Formats(FormatClassEmitted, "Formats");
      for (const TGInstrLayout &Inst : InstFormats) {
        Inst.emitFormat(FieldsHierarchy, Formats, OpFields, FieldRanges,
                        SlotsFields);
        Formats.next();
      }

      Formats.finish();
      OpFields.finish();
      SlotsFields.finish();
      FieldRanges.finish();

      o << OpFields;
      o << FieldRanges;
      o << SlotsFields;
      o << Formats;
      o << "#endif // GET_FORMATS_FORMATS_DEFS\n\n";
    }

    o << "#ifdef GET_FORMATS_PACKETS_TABLE\n"
      << "#undef GET_FORMATS_PACKETS_TABLE\n\n";

    // Introduce an intermediate container for the Packets which allow us
    // to sort them (as the base container can't be sorted as TGInstLayout
    // isn't Copy-assignable).
    std::vector<const TGInstrLayout *> Packets;

    for (TGInstrLayout &Inst : InstFormats)
      if (Inst.isPacketFormat()) {
        Packets.push_back(&Inst);
      }

    // Sort the Packets by (size, slotSet)
    std::sort(Packets.begin(), Packets.end(),
              [](const TGInstrLayout *Packet0, const TGInstrLayout *Packet1) {
                return Packet0->getSize() != Packet1->getSize()
                           ? Packet0->getSize() < Packet1->getSize()
                           : Packet0->getSlotSet() < Packet1->getSlotSet();
              });
    {
      // Create two flat tables, one holding all slot ranges
      // consecutively, the other with the formats,
      // referencing the ranges
      ConstTable SlotData("MCSlotKind", "FormatSlotData");
      ConstTable FormatData("VLIWFormat", "FormatData");

      // Emit each Packet in Size order
      for (const TGInstrLayout *Packet : Packets)
        Packet->emitPacketEntry(FormatData, SlotData);

      // Terminate both tables
      SlotData.finish();
      // Add a sentinel, (The 0 opcode is used by the getFormat lookup)
      FormatData.mark("Sentinel");
      FormatData << "{\n  0, nullptr, {nullptr, nullptr}, 0, 0}\n";
      FormatData.finish();

      o << SlotData << FormatData
        << "static const PacketFormats Formats {FormatData};\n\n";

      // Create an O(1) function of available formats by ticking them in
      // a lookup table;
      uint64_t MaxSlotSet = 0;
      for (const TGInstrLayout *Packet : Packets) {
        MaxSlotSet = std::max(MaxSlotSet, Packet->getSlotSet());
      }
      const size_t Size = MaxSlotSet + 1;
      // Catering for 16 slots for now, just to check the logic.
      assert(Size <= 65536);
      std::vector<bool> LUT(Size, false);
      // First the trivial ones, without nops.
      for (const TGInstrLayout *Packet : Packets) {
        LUT[Packet->getSlotSet()] = true;
      }
      // Then the remaining ones
      for (uint64_t SlotSet = 0; SlotSet < Size; SlotSet++) {
        if (LUT[SlotSet]) {
          continue;
        }
        // We start with the largest packets, since they cover more.
        for (const TGInstrLayout *Packet : reverse(Packets)) {
          // If the packet clears all bits of SlotSet, it can hold all these
          // slots. Slots that are not present in SlotSet can be filled
          // with nops.
          if ((SlotSet & ~Packet->getSlotSet()) == 0) {
            LUT[SlotSet] = true;
            break;
          }
        }
      }
      o << "const size_t SlotSetSize = " << Size << ";\n";
      o << "static const bool FormatAvailable[SlotSetSize] = {\n";
      for (size_t Index = 0; Index < Size; Index++) {
        o << "\t/* " << Index << " */ " << LUT[Index] << ",\n";
      }
      o << "};\n";

      o << "#endif // GET_FORMATS_PACKETS_TABLE\n\n";
    }
  }
}

namespace {
/// Suffix strings for each slot, indexed 0/1/2. Product contract: the suffix
/// digit IS the slot (vector index == field/slot for sparse size-3
/// AlternateInsts).
constexpr const char *SlotMemberSuffix[3] = {"_S0", "_S1", "_S2"};
} // end anonymous namespace

/// Audit helper: map logical opcodes → `_S{0,1,2}` members by suffix.
/// Not a product alternate source — emitAlternateInstsOpcodeFunc fails closed
/// if any setDesc-safe row remains uncovered by MultiSlot/LogicalMaterialize.
static std::map<unsigned, SlotMemberVariantRow>
collectSparseAltSlotMemberRows(
    ArrayRef<const CodeGenInstruction *> NumberedInstructions) {
  StringMap<unsigned> NameToEnum;
  for (unsigned Opc = 0, E = NumberedInstructions.size(); Opc < E; ++Opc)
    NameToEnum[NumberedInstructions[Opc]->TheDef->getName()] = Opc;

  std::map<unsigned, SlotMemberVariantRow> RowsByName;
  for (unsigned Opc = 0, E = NumberedInstructions.size(); Opc < E; ++Opc) {
    StringRef Name = NumberedInstructions[Opc]->TheDef->getName();
    for (unsigned Slot = 0; Slot < 3; ++Slot) {
      StringRef Suffix = SlotMemberSuffix[Slot];
      if (!Name.ends_with(Suffix))
        continue;
      StringRef Base = Name.drop_back(Suffix.size());
      auto It = NameToEnum.find(Base);
      if (It == NameToEnum.end())
        continue; // Unmapped member (no logical base) — decoder-only.
      RowsByName[It->second].Members[Slot] = Opc;
    }
  }
  return RowsByName;
}

/// Place a member opcode into a size-3 row by residual `_S{N}` suffix or by
/// InstSlot SlotName S0/S1/S2. Returns false if no field can be resolved.
static bool placeMemberInSparseRow(const CodeGenInstruction &Member,
                                   unsigned MemberOpc,
                                   SlotMemberVariantRow &Row) {
  StringRef Name = Member.TheDef->getName();
  for (unsigned Slot = 0; Slot < 3; ++Slot) {
    if (Name.ends_with(SlotMemberSuffix[Slot])) {
      Row.Members[Slot] = MemberOpc;
      return true;
    }
  }
  // Typed Slot field (s0_slot / s1_slot / s2_slot → SlotName S0/S1/S2).
  if (Member.TheDef->getValue("Slot")) {
    const Record *SlotRec = Member.TheDef->getValueAsDef("Slot");
    if (SlotRec && SlotRec->getValue("SlotName")) {
      StringRef SlotName = SlotRec->getValueAsString("SlotName");
      for (unsigned Slot = 0; Slot < 3; ++Slot) {
        // SlotMemberSuffix is "_S0"; InstSlot SlotName is "S0".
        if (SlotName == StringRef(SlotMemberSuffix[Slot]).drop_front(1)) {
          Row.Members[Slot] = MemberOpc;
          return true;
        }
      }
    }
  }
  return false;
}

/// Bulk declarative LogicalMaterialize rows (typed alternate relation).
/// Same sparse size-3 shape as MultiSlot materializableInto emission.
static std::map<unsigned, SlotMemberVariantRow>
collectLogicalMaterializeRows(
    ArrayRef<const CodeGenInstruction *> NumberedInstructions,
    const RecordKeeper &Records) {
  StringMap<unsigned> NameToEnum;
  for (unsigned Opc = 0, E = NumberedInstructions.size(); Opc < E; ++Opc)
    NameToEnum[NumberedInstructions[Opc]->TheDef->getName()] = Opc;

  std::map<unsigned, SlotMemberVariantRow> Rows;
  if (!Records.getClass("LogicalMaterialize"))
    return Rows;

  for (const Record *D :
       Records.getAllDerivedDefinitions("LogicalMaterialize")) {
    if (!D->getValue("BaseLogical") || !D->getValue("materializableInto"))
      continue;
    const Record *Base = D->getValueAsDef("BaseLogical");
    auto BaseIt = NameToEnum.find(Base->getName());
    if (BaseIt == NameToEnum.end()) {
      PrintFatalError(D->getLoc(),
                      "LogicalMaterialize BaseLogical '" + Base->getName() +
                          "' not found among instructions");
    }
    SlotMemberVariantRow &Row = Rows[BaseIt->second];
    for (const Record *MemRec :
         D->getValueAsListOfDefs("materializableInto")) {
      auto MemIt = NameToEnum.find(MemRec->getName());
      if (MemIt == NameToEnum.end()) {
        PrintFatalError(D->getLoc(),
                        "LogicalMaterialize member '" + MemRec->getName() +
                            "' for '" + Base->getName() +
                            "' not found among instructions");
      }
      const CodeGenInstruction &MemberCGI =
          *NumberedInstructions[MemIt->second];
      if (!placeMemberInSparseRow(MemberCGI, MemIt->second, Row)) {
        PrintFatalError(MemRec->getLoc(),
                        "LogicalMaterialize member '" + MemRec->getName() +
                            "' has no S0/S1/S2 suffix or InstSlot field");
      }
    }
  }
  return Rows;
}

/// Emit GET_ALTERNATE_INST_OPCODE_FUNC (AIE MultiSlot_Pseudo + Haydn
/// LogicalMaterialize bulk typed relation). No residual `_S*` name-discovery
/// backfill: uncovered setDesc-safe sparse rows fail closed at TableGen.
/// AIE peer: CodeGenFormat emit ~142-164 and addAlternateInstInMultiSlotPseudo
/// (AIE dense materializableInto order). Haydn sparse-alt rows are always
/// size 3 with 0 for missing slots so vector index == field/slot.
///
/// Precondition: validateSetDescAlternateCompatibility has already proven
/// MultiSlot pairs are setDesc-safe; LogicalMaterialize rows pass structural
/// setDesc filtering below.
static void emitAlternateInstsOpcodeFunc(
    raw_ostream &o, const CodeGenTarget &Target,
    ArrayRef<const CodeGenInstruction *> NumberedInstructions,
    const std::vector<TGInstrLayout> &PseudoInstFormats,
    const RecordKeeper &Records) {
  const std::string TargetName = Target.getName().str();

  // Names already covered by true MultiSlot_Pseudo (materializableInto).
  std::set<std::string> PseudoNames;
  for (const TGInstrLayout &P : PseudoInstFormats)
    PseudoNames.insert(P.getInstrName());

  struct SparseAltEntry {
    std::string LogicalName;
    // Always length 3: Target::Name or "0" at missing slots (sparse alts).
    std::string Members[3];
  };

  auto rowToEntry =
      [&](unsigned LogicalOpc,
          const SlotMemberVariantRow &Row) -> std::optional<SparseAltEntry> {
    StringRef LogicalName =
        NumberedInstructions[LogicalOpc]->TheDef->getName();
    if (PseudoNames.count(LogicalName.str()))
      return std::nullopt;
    SparseAltEntry Entry;
    Entry.LogicalName = LogicalName.str();
    bool Any = false;
    for (unsigned Slot = 0; Slot < 3; ++Slot) {
      if (Row.Members[Slot] == 0) {
        Entry.Members[Slot] = "0";
        continue;
      }
      Any = true;
      StringRef MemName =
          NumberedInstructions[Row.Members[Slot]]->TheDef->getName();
      Entry.Members[Slot] = TargetName + "::" + MemName.str();
    }
    if (!Any)
      return std::nullopt;
    return Entry;
  };

  // Bulk declarative LogicalMaterialize (typed alternate relation).
  auto MaterializeRows = filterSparseAltsSetDescStructural(
      NumberedInstructions,
      collectLogicalMaterializeRows(NumberedInstructions, Records));
  std::set<unsigned> CoveredLogicals;
  std::vector<SparseAltEntry> SparseAlts;
  SparseAlts.reserve(MaterializeRows.size());
  for (const auto &KV : MaterializeRows) {
    if (auto E = rowToEntry(KV.first, KV.second)) {
      CoveredLogicals.insert(KV.first);
      SparseAlts.push_back(std::move(*E));
    }
  }

  // Fail closed: no suffix-discovered alternate emission. Any setDesc-safe
  // `_S*` row whose logical is not MultiSlot_Pseudo / LogicalMaterialize is a
  // TableGen error (forces explicit materializableInto).
  auto SparseAuditRows = filterSparseAltsSetDescStructural(
      NumberedInstructions,
      collectSparseAltSlotMemberRows(NumberedInstructions));
  for (const auto &KV : SparseAuditRows) {
    if (CoveredLogicals.count(KV.first))
      continue;
    StringRef LogicalName =
        NumberedInstructions[KV.first]->TheDef->getName();
    if (PseudoNames.count(LogicalName.str()))
      continue;
    bool Any = false;
    for (unsigned Slot = 0; Slot < 3; ++Slot)
      if (KV.second.Members[Slot] != 0)
        Any = true;
    if (!Any)
      continue;
    PrintFatalError(
        NumberedInstructions[KV.first]->TheDef->getLoc(),
        "logical '" + LogicalName.str() +
            "' has setDesc-safe `_S*` slot members but no MultiSlot_Pseudo "
            "or LogicalMaterialize materializableInto list; suffix name "
            "discovery is not a product alternate source");
  }

  const unsigned NumPseudo = PseudoInstFormats.size();
  const unsigned NumSparseAlt = static_cast<unsigned>(SparseAlts.size());
  const unsigned NumTotal = NumPseudo + NumSparseAlt;

  o << "#ifdef GET_ALTERNATE_INST_OPCODE_FUNC\n"
    << "#undef GET_ALTERNATE_INST_OPCODE_FUNC\n";

  if (NumTotal != 0) {
    o << "// Alternate member opcodes per multi-slot logical.\n"
      << "// Order: MultiSlot_Pseudo materializableInto (AIE), then\n"
      << "// LogicalMaterialize bulk typed relation. No residual `_S*`\n"
      << "// name-discovery backfill. All sparse rows are size-3\n"
      << "// (index==field, 0=hole). MultiSlot members are fully setDesc-safe;\n"
      << "// LogicalMaterialize members pass structural shape filtering.\n"
      << "static std::vector<unsigned int> const AlternateInsts[] = {\n";
    for (unsigned I = 0; I < NumPseudo; ++I) {
      PseudoInstFormats[I].emitAlternateInstsOpcodeSet(o);
      if (I + 1 != NumTotal)
        o << ", \n";
    }
    for (unsigned I = 0; I < NumSparseAlt; ++I) {
      const SparseAltEntry &E = SparseAlts[I];
      o << "    // " << TargetName << "::" << E.LogicalName
        << " (LogicalMaterialize, sparse size-3)\n";
      o << "    { " << E.Members[0] << ", " << E.Members[1] << ", "
        << E.Members[2] << " }";
      if (NumPseudo + I + 1 != NumTotal)
        o << ", \n";
    }
    o << "\n};\n\n";
  }

  o << "const std::vector<unsigned int> *" << TargetName
    << "MCFormats::getAlternateInstsOpcode";
  o << "(unsigned int Opcode) const {\n";
  o << "  switch (Opcode) {\n";
  o << "  default:\n";
  o << "    return nullptr;\n";
  for (unsigned I = 0; I < NumPseudo; ++I)
    PseudoInstFormats[I].emitAlternateInstsOpcode(o, I);
  for (unsigned I = 0; I < NumSparseAlt; ++I) {
    const SparseAltEntry &E = SparseAlts[I];
    o << "  case " << TargetName << "::" << E.LogicalName << ":\n"
      << "    return &AlternateInsts[" << (NumPseudo + I) << "];\n";
  }
  o << "  }\n}\n";
  o << "#endif // GET_ALTERNATE_INST_OPCODE_FUNC\n\n";
}


void CodeGenFormat::computeSlotSets(TGTargetSlots &Slots,
                                    std::vector<TGInstrLayout> &InstFormats) {

  // The slots accommodated by each format.
  for (TGInstrLayout &Format : InstFormats) {
    if (Format.isPacketFormat()) {
      Format.computeSlotSet();
    }
  }

  // The universe of all slots
  uint64_t AllSlots = 0;
  for (const auto &[_, Slot] : Slots) {
    if (Slot.isDefaultSlot() || Slot.isArtificial()) {
      continue;
    }
    AllSlots |= Slot.getSlotBits();
  }

  // Compute the conflict bits of all slots.
  for (unsigned S = 0; S < Slots.size(); S++) {
    // Effectively we check whether a slot cannot be combined with another
    // slot in any format in which the first is accommodated. We start out
    // with all slots excluded, and eliminate the exclusion when a format is
    // encountered that allows both ThisSlot and the exclusion.
    auto &Slot = Slots.getSlot(S).second;
    const uint64_t ThisSlot = Slot.getSlotBits();
    uint64_t Excluded = AllSlots;
    for (TGInstrLayout &Format : InstFormats) {
      if (!Format.isPacketFormat()) {
        continue;
      }
      uint64_t SlotSet = Format.getSlotSet();
      if (SlotSet & ThisSlot) {
        Excluded &= ~SlotSet & AllSlots;
      }
    }
    Slot.setConflictBits(ThisSlot | Excluded);
  }
}

// Retrieve the number of consecutive bits (from BitPos) that are part of the
// same "variable" (described by "VarName")
// NOTE0: if the bit at BitPos isn't a variable bit, then we simply return 0
// NOTE1: the counting is made in descending order (i.e from bit n-1, left one,
//        to bit 0)
unsigned CodeGenFormat::getVariableBits(const std::string &VarName,
                                        const BitsInit *BI, unsigned BitPos) {
  assert(BI && "BI pointer must be non-null");
  assert(BitPos < BI->getNumBits() && "BitPos out of range");

  unsigned Counter = 0;
  for (int Bit = static_cast<int>(BitPos); Bit >= 0; Bit--, Counter++) {
    const VarBitInit *const VBI = dyn_cast<const VarBitInit>(BI->getBit(Bit));
    if (VBI) {
      const VarInit *const VI = dyn_cast<const VarInit>(VBI->getBitVar());
      // if the name is different, stop counting
      if (!VI || VI->getName() != VarName) {
        break;
      }
    } else {
      break;
    }
  }
  return Counter;
}

// Retrieve the number of consecutive bits (from BitPos) that are part of the
// same chunck of Fixed bits (placed into the OutChunck)
unsigned CodeGenFormat::getFixedBits(std::string &OutChunck, const BitsInit *BI,
                                     unsigned BitPos) {
  assert(BI && "BI pointer must be non-null");
  assert(BitPos < BI->getNumBits() && "BitPos out of range");

  OutChunck = "";
  unsigned Counter = 0;
  for (int Bit = static_cast<int>(BitPos); Bit >= 0; Bit--, Counter++) {
    const BitInit *BInit = nullptr;
    BInit = dyn_cast<const BitInit>(BI->getBit(Bit));
    if (!BInit) {
      break;
    }
    bool Value = BInit->getValue();
    OutChunck += Value ? "1" : "0";
  }
  return Counter;
}

TGInstrLayout::TGInstrLayout(const CodeGenInstruction *const CGI,
                             const TGTargetSlots &Slots)
    : CGI(CGI), SlotsRegistry(Slots), IsComposite(false),
      IsMultipleSlotOptions(false), IsSlotNOP(false), InstrID(EmissionID++) {

  // resolveIsMultipleSlotOptions is required to select Arttibute to keep track
  // of.
  resolveIsMultipleSlotOptions();

  // Default name of Instruction Attribute in TableGen record
  std::string InstrAttribute =
      IsMultipleSlotOptions ? "isMultiSlotPseudo" : "Inst";

  assert(CGI && "null pointer as CodeGenInstruction argument");
  assert(CGI->TheDef && "CodeGenInstruction doesn't own any Record reference");
  assert(CGI->TheDef->getValue(InstrAttribute) &&
         "Expected Instruction Definition in the Record");
  assert(Slots.isFinalized() && "Slots Pool must be finalized");

  const Record *BaseRecord = CGI->TheDef;
  InstrName = BaseRecord->getName().str();
  Target = BaseRecord->getValueAsString("Namespace").str();
  Size = BaseRecord->getValueAsBitsInit(InstrAttribute)->getNumBits();

  InstField = std::make_shared<TGFieldLayout>(
      CGI, InstrAttribute, nullptr, Size, TGFieldLayout::FieldType::Variable);

  if (IsMultipleSlotOptions) {
    std::vector<const Record *> SlotRecords =
        CGI->TheDef->getValueAsListOfDefs("materializableInto");
    assert(!SlotRecords.empty() &&
           "Expected Instructions for multi slot Pseudo instructions");
    resolveIsComposite();
    return;
  }

  const BitsInit *BI = nullptr;
  unsigned HierarchyLevel = 0;

  // Find the super Class where the definition of "Inst" begin
  // This "Level" in the hierarchy will be stored in the HierarchyLevel
  for (const Record *SuperClass : BaseRecord->getSuperClasses()) {
    if (SuperClass->getValue(InstrAttribute)) {
      BI = SuperClass->getValueAsBitsInit(InstrAttribute);
      break;
    }
    ++HierarchyLevel;
  }
  (void)BI;
  assert(BI && "No Bits Init found from the base Instr Attribute");

  // Recursively search of every fields definition
  // Recursion will begin with the "Inst" field
  InstField->resolveFieldsDefInHierarchy(InstrAttribute, HierarchyLevel,
                                         InstField);

  // Fields definition must be resolved before making the offsets resolution
  for (auto *Field : leaves()) {
    Field->resolveFieldsGlobalOffsets();
  }

  // Here, the order matters: IsComposite has to be resolved first as we use
  // this property to resolve IsSlot
  resolveIsComposite();
  resolveIsSlot();
  resolveMCOperandNumber();
  resolveIsSlotNOP();
}

TGInstrLayout::NOPSlotMap TGInstrLayout::NOPMapper;
unsigned TGInstrLayout::EmissionID = 0;

unsigned TGInstrLayout::getSize() const { return InstField->getSize(); }

void TGInstrLayout::dump(std::ostream &stream) const {
  stream << InstrName << " format decomposition:" << std::endl;
  ;
  InstField->dump(stream);
  stream << std::endl;
}

void TGInstrLayout::resolveIsComposite() {
  const Record *BaseRecord = CGI->TheDef;
  if (BaseRecord->getValue("isComposite"))
    IsComposite = BaseRecord->getValueAsBit("isComposite");
}

void TGInstrLayout::resolveIsMultipleSlotOptions() {
  const Record *BaseRecord = CGI->TheDef;
  if (BaseRecord->getValue("isMultiSlotPseudo"))
    IsMultipleSlotOptions =
        BaseRecord->getValueAsBitsInit("isMultiSlotPseudo")->getBit(0);
}

void TGInstrLayout::addAlternateInstInMultiSlotPseudo(
    const std::vector<TGInstrLayout> &InstFormats) {

  std::vector<const Record *> AltInsts =
      CGI->TheDef->getValueAsListOfDefs("materializableInto");

  std::set<const TGTargetSlot *> SupportedSlot;
  for (const Record *AltInst : AltInsts) {
    bool Found = false;
    for (const TGInstrLayout &Inst : InstFormats) {
      if (Inst.InstrName == AltInst->getName()) {
        assert(hasSingleElement(Inst.slots()) &&
               "Real Instr should have only one slot");
        for (const auto &SlotField : Inst.slots()) {
          const TGTargetSlot *Slot = SlotField->SlotClass;
          if (Slot) {
            // Add alternate instr only if no other instr with same slot exist
            if (SupportedSlot.find(Slot) == SupportedSlot.end()) {
              addAlternateInsts(Target + "::" + Inst.InstrName);
              SupportedSlot.insert(Slot);
            } else
              llvm_unreachable(
                  "An instr. with same slot already added to supported list");
          } else
            llvm_unreachable(
                "MulitSlot Pseudo Inst. with an Real Inst having UNKNOWN slot");
        }
        Found = true;
        break;
      }
    }
    if (!Found)
      llvm_unreachable("Instruction for MulitSlot Pseudo Inst. not found");
  }
}

void TGInstrLayout::resolveIsSlot() {
  // We're dealing with a Packet Format
  if (IsComposite) {
    // For all the leaves that are not fixed bits
    for (auto *Field : leaves()) {
      if (Field->isFixedBits())
        continue;

      if (std::optional<unsigned> OpIdxOpt =
              CGI->Operands.findOperandNamed(Field->Label)) {
        unsigned OpIdx = *OpIdxOpt;
        // find by Record
        TGTargetSlots::const_iterator SlotIt =
            SlotsRegistry.find(CGI->Operands[OpIdx].Rec);
        if (SlotIt != SlotsRegistry.end())
          Field->setSlot(SlotIt->second);
        else {
          dbgs() << "Operand " << CGI->Operands[OpIdx].Rec->getName()
                 << " skipped when resolving Slot information for the "
                    "composite record "
                 << CGI->TheDef->getName() << "\n";
        }
      }
    }
  } else {
    // Slot of the BaseRecord
    const Record *SlotRecord = CGI->TheDef->getValueAsDef("Slot");
    // If it's the default slot, then abort the procedure
    if (SlotRecord == SlotsRegistry.getDefaultSlot()->first)
      return;
    // Entry of the SlotRecord in the slot pool
    TGTargetSlots::const_iterator SlotPtr = SlotsRegistry.find(SlotRecord);

    // If the slot on which the Record is pointing isn't in the slot pool
    // This should not happened as the Backend has to crash when an invalid
    // slot is detected.
    if (SlotPtr == SlotsRegistry.end()) {
      dbgs() << "error: Record " << InstrName
             << " pointing to an invalid "
                "(unregistered) slot: "
             << SlotRecord->getName() << "\n";
      exit(EXIT_FAILURE);
    }

    // Intermediate variables for the sake of clarity
    const std::string &SlotName = SlotPtr->second.getInstanceName();
    const std::string &LabelToFind = SlotPtr->second.getLabelToFind();

    // Lambda returning true if a valid slot is evaluated
    auto IsValidSlot = [&](const TGFieldLayout &FL) -> bool {
      return (FL.getLabel() == LabelToFind &&
              FL.getSize() == SlotPtr->second.getSlotSize());
    };

    // Defining an iterator over the fields with the predicate defined above.
    // NOTE: the Mode specified here isn't important as when we will encounter
    // the good slot field, we're gonna register it and then stop the research.
    // So, there is no point defining what we're going to do after the
    // predicate returns true...
    iterator_range<TGFieldIterator> IsValidSlotRange = make_range(
        TGFieldIterator(InstField.get(), TGFieldIterator::Mode::StopTraversal,
                        IsValidSlot),
        TGFieldIterator(nullptr));

    bool SlotResolved = false;
    for (auto *Field : IsValidSlotRange) {
      Field->setSlot(SlotPtr->second);
      // Make sure we really enter into that loop
      SlotResolved = true;
      break;
    }

    if (!SlotResolved) {
      dbgs() << "error: " << SlotName << " not resolved for instruction "
             << InstrName << "\n"
             << "NOTE: occurs when trying to find the field with label \""
             << LabelToFind
             << "\" in the format encoding of the instruction\n\n";
      exit(EXIT_FAILURE);
    }
  }
}

void TGInstrLayout::resolveMCOperandNumber() {
  // Property:
  // A Field which represents an MCOperand is always a leaf of the tree
  for (auto *Field : leaves()) {
    if (Field->isFixedBits())
      continue;

    // If the operand matches by name, reference according to that
    // operand number. Non-matching operands are assumed to be in
    // order.
    // NOTE (Haydn port): upstream replaced CGIOperandList::hasOperandNamed
    // (out-param) with findOperandNamed (returns std::optional<unsigned>); and
    // removed isFlatOperandNotEmitted entirely (vestigial do-not-encode assert).
    if (std::optional<unsigned> OpIdxOpt =
            CGI->Operands.findOperandNamed(Field->Label)) {
      unsigned OpIdx = *OpIdxOpt;
      // Get the machine operand number for the indicated operand.
      OpIdx = CGI->Operands[OpIdx].MIOperandNo;
      Field->setMCOperandIndex(OpIdx);
    }
  }
}

void TGInstrLayout::resolveIsSlotNOP() {
  const Record *BaseRecord = CGI->TheDef;
  IsSlotNOP = BaseRecord->getValueAsBit("isSlotNOP");
  // If the instruction is a NOP representative of the slot
  // (isPacketFormat is an extra check...)
  if (isSlotNOP() && !isPacketFormat()) {
    const Record *R = BaseRecord->getValueAsDef("Slot");
    if (SlotsRegistry.getDefaultSlot()->first == R) {
      errs() << "error: " << InstrName
             << " has been defined as the NOP "
                "representative of the default slot.\n";
      exit(EXIT_FAILURE);
    }
    NOPMapper.insert({R, InstrName});
  }
}

iterator_range<TGFieldIterator> TGInstrLayout::fields() const {
  return make_range(TGFieldIterator(InstField.get()), TGFieldIterator(nullptr));
}

iterator_range<TGFieldIterator> TGInstrLayout::leaves() const {
  // Create a predicate returning true iff the field is a terminal (i.e. leaf)
  auto TerminalPolicy = [](const TGFieldLayout &FL) -> bool {
    return FL.isLeaf();
  };

  // Build an iterator on the terminals performing a full traversal
  return make_range(TGFieldIterator(InstField.get(),
                                    TGFieldIterator::Mode::FullTraversal,
                                    TerminalPolicy),
                    TGFieldIterator(nullptr));
}

iterator_range<TGFieldIterator> TGInstrLayout::slots() const {
  // Create a predicate returning true iff the field is describing a slot
  auto SlotPolicy = [](const TGFieldLayout &FL) -> bool { return FL.isSlot(); };

  return make_range(TGFieldIterator(InstField.get(),
                                    TGFieldIterator::Mode::StopTraversal,
                                    SlotPolicy),
                    TGFieldIterator(nullptr));
}

iterator_range<TGFieldIterator> TGInstrLayout::operands() const {
  // This predicate returns true iff the field owns a valid MCOperand index
  auto OperandPolicy = [](const TGFieldLayout &FL) -> bool {
    return FL.isLeaf() && FL.getMCOperandIndex() != -1;
  };

  return make_range(TGFieldIterator(InstField.get(),
                                    TGFieldIterator::Mode::FullTraversal,
                                    OperandPolicy),
                    TGFieldIterator(nullptr));
}

void TGInstrLayout::emitFlatTree(ConstTable &FieldsHierarchy,
                                 unsigned &FieldIndex) const {
  const std::string GenSlotKindName = SlotsRegistry.GenSlotKindName;
  for (TGFieldLayout *Field : fields()) {
    Field->EmissionID = FieldIndex++;
  }
  FieldsHierarchy << "    // " << Target << "::" << InstrName
                  << " - Index : " << std::to_string(InstrID)
                  << " - FieldIndex : " << FieldsHierarchy.mark() << "\n";
  const char *Bracket = "{ ";
  for (const TGFieldLayout *Field : fields()) {
    FieldsHierarchy << Bracket;
    Bracket = "     { ";
    if (Field->isGlobalPositionKnown())
      FieldsHierarchy << " MCFormatField::GlobalOffsets{ "
                      << Field->GlobalOffsets.LeftOffset << ", "
                      << Field->GlobalOffsets.RightOffset << " }, ";
    else
      FieldsHierarchy << " /*LocInfos=*/std::nullopt,";

    const std::string TargetKindName = "MC" + SlotsRegistry.GenSlotKindName;
    const TGTargetSlot *SlotInfo = Field->getSlot();
    const TGTargetSlots::RecordSlot *DefaultSlotInfo =
        SlotsRegistry.getDefaultSlot();

    FieldsHierarchy << TargetKindName << '(' << TargetKindName << "::";
    if (SlotInfo)
      FieldsHierarchy << SlotInfo->getEnumerationString();
    else
      FieldsHierarchy << DefaultSlotInfo->second.getEnumerationString();
    FieldsHierarchy << ") }";
    FieldsHierarchy.next();
  }
}

void TGInstrLayout::emitAlternateInstsOpcodeSet(raw_ostream &o) const {
  assert(AlternateInsts.size() &&
         "AlternateInsts cannot be empty for multi slot pseudo instr");
  // Emit sparse size-3 (index == field/slot; 0 = hole) so PlacementAlternative
  // FieldSlots = 1<<index matches the member's issue field. Declarative
  // materializableInto lists may omit holes (e.g. S1+S2 only); place each
  // member by residual _S{N} suffix when present, else densify list order
  // into the first N slots (AIE consecutive MultiSlot shape).
  std::string Members[3] = {"0", "0", "0"};
  bool AnySuffix = false;
  for (const std::string &AltInstr : AlternateInsts) {
    StringRef Leaf = stripTargetNamespace(AltInstr);
    for (unsigned Slot = 0; Slot < 3; ++Slot) {
      if (!Leaf.ends_with(SlotMemberSuffix[Slot]))
        continue;
      Members[Slot] = AltInstr;
      AnySuffix = true;
      break;
    }
  }
  if (!AnySuffix) {
    for (unsigned I = 0, E = static_cast<unsigned>(AlternateInsts.size());
         I < E && I < 3; ++I)
      Members[I] = AlternateInsts[I];
  }
  o << "    // " << Target << "::" << InstrName
    << " (MultiSlot_Pseudo materializableInto, sparse size-3)\n";
  o << "    { " << Members[0] << ", " << Members[1] << ", " << Members[2]
    << " }";
}

void TGInstrLayout::emitAlternateInstsOpcode(raw_ostream &o,
                                             unsigned int index) const {
  assert(AlternateInsts.size() &&
         "AlternateInsts cannot be empty for multi slot pseudo instr");
  o << "  case " << Target << "::" << InstrName << ":\n"
    << "    return &AlternateInsts[" << std::to_string(index) << "];\n";
}

void TGInstrLayout::emitOpcodeFormatIndex(raw_ostream &o) const {
  o << "  case " << Target << "::" << InstrName << ":\n"
    << "    return " << std::to_string(InstrID) << ";\n";
}

void TGInstrLayout::emitFormat(ConstTable &FieldsHierarchy, ConstTable &o,
                               ConstTable &OpFields, ConstTable &FieldRanges,
                               ConstTable &SlotsFields) const {
  o << "    // " << Target << "::" << InstrName
    << " - Index : " << std::to_string(InstrID) << "\n"
    << "    {\n"
    << "      " << /*"llvm" << "::" << Namespace << "::" <<*/ InstrName
    << " /* Opcode */,\n"
    << "      " << (IsComposite ? "true" : "false") << " /* isComposite */,\n"
    << "      " << (IsMultipleSlotOptions ? "true" : "false")
    << " /* hasMultipleSlotOptions */,\n"
    << "      /* Slots - Fields mapper */\n";

  const std::string TargetClassName = "MC" + SlotsRegistry.GenSlotKindName;
  SlotsFields.mark(InstrName.c_str());

  for (const auto &SlotField : slots()) {
    SlotsFields << "{ ";
    SlotsFields << TargetClassName
                << "::" << SlotField->SlotClass->getEnumerationString() << ", ";
    SlotsFields << FieldsHierarchy.absRef(SlotField->EmissionID);
    SlotsFields << " }";
    SlotsFields.next();
  }
  o << "      " << SlotsFields.arrayRef() << ",\n";

  // Group every field by their operand index (if they have one)
  // i.e. { 0 : {field0, field1}, 1 : {field2}}...
  std::map<unsigned, SmallVector<TGFieldLayout *>> FieldOpIndexMap;
  // Keep track of the maximum operand index
  unsigned NumOperands = 0;
  for (auto *OperandField : operands()) {
    unsigned OperandIdx = OperandField->getMCOperandIndex();
    FieldOpIndexMap[OperandIdx].push_back(OperandField);
    NumOperands = std::max(NumOperands, OperandIdx);
  }

  o << "      /* MCOperand - Slots mapper */\n";
  OpFields.mark(InstrName.c_str());
  FieldRanges.mark(InstrName.c_str());
  unsigned OpFieldIdx = 0;
  for (unsigned OperandIdx = 0; OperandIdx <= NumOperands; OperandIdx++) {
    FieldRanges << OpFields.ref(OpFieldIdx);
    FieldRanges.next();
    auto It = FieldOpIndexMap.find(OperandIdx);
    if (It != FieldOpIndexMap.end()) {
      for (auto &MO : It->second) {
        OpFields << "  " << FieldsHierarchy.absRef(MO->EmissionID);
        OpFields.next();
        OpFieldIdx++;
      }
    }
  }
  o << "      {" << FieldRanges.ref(0) << ", " << NumOperands + 1 << "},\n";

  auto *RootField = *fields().begin();
  o << "      " << FieldsHierarchy.absRef(RootField->EmissionID) << "\n"
    << "    }";
}

void TGInstrLayout::emitPacketEntry(ConstTable &Packets,
                                    ConstTable &SlotData) const {
  // Some instructions (DUMMY96, UNKNOWN128...) are indicated as Composite but
  // they don't define a Packet Format... In that case, we don't emit anything.
  if (slots().begin() == slots().end())
    return;

  Packets << "{\n"
          << "  " << Target << "::" << getInstrName() << ",\n"
          << "  \"" << getInstrName() << "\",\n";

  const std::string TargetSlotKindName = "MC" + SlotsRegistry.GenSlotKindName;

  SlotData << "// " << getInstrName() << " : " << SlotData.mark() << "\n";
  for (auto *Slot : slots()) {
    SlotData << TargetSlotKindName
             << "::" << Slot->getSlot()->getEnumerationString();
    SlotData.next();
  }
  Packets << "  { " << SlotData.ref(0) << "," << SlotData.refNext() << "},\n";
  Packets << "  " << getSize() / 8 << std::dec << ",\n";
  Packets << "  " << std::hex << std::showbase << SlotSet << std::dec << "},\n";
}

/// Precompute the slot bits for sorting
void TGInstrLayout::computeSlotSet() {
  uint64_t Bits = 0;
  for (const auto *Slot : slots()) {
    Bits |= uint64_t(1) << Slot->getSlot()->getNumSlot();
  }
  SlotSet = Bits;
}

/// Returns true whether all of the bits are not complete
static bool areAllBitsNotComplete(const BitsInit *BI) {
  unsigned E = BI->getNumBits();
  for (unsigned B = 0; B != E; ++B) {
    if (BI->getBit(B)->isComplete()) {
      return false;
    }
  }
  return true;
}

void TGFieldLayout::resolveFieldsDefInBaseRecord(
    const std::string &LabelToFind, const TGFieldLayoutPtr &BaseFieldPtr) {
  const Record *const BaseRecord = CGI->TheDef;
  const BitsInit *BI = nullptr;

  if (BaseRecord->getValue(LabelToFind))
    BI = BaseRecord->getValueAsBitsInit(LabelToFind);

  if (!BI) {
    // We will not find any Record defining this fields
    return;
  }
  DefRecord = BaseRecord;

  const VarBitInit *VBI = nullptr;
  const VarInit *VI = nullptr;
  const BitInit *BtI = nullptr;

  for (int NBits = BI->getNumBits() - 1; NBits >= 0;) {
    VBI = nullptr;
    VI = nullptr;
    BtI = nullptr;
    std::string Componentlabel;

    if ((VBI = dyn_cast<VarBitInit>(BI->getBit(NBits)))) {
      VI = dyn_cast<VarInit>(VBI->getBitVar());
      if (VI)
        Componentlabel = VI->getName().str();
    } else {
      BtI = dyn_cast<BitInit>(BI->getBit(NBits));
    }

    if (VI) {
      // See if there are other "variable" bits to group in the current
      // Variable.
      // NOTE: getVariableBits should always return >= 0 value in this context
      // as we know that there is at least one bit inside so varsize >= 1
      unsigned VarSize =
          CodeGenFormat::getVariableBits(Componentlabel, BI, NBits);

      std::shared_ptr<TGFieldLayout> const &FL =
          BaseFieldPtr->addSubField(std::make_shared<TGFieldLayout>(
              CGI, Componentlabel, BaseFieldPtr, VarSize,
              TGFieldLayout::FieldType::Variable));

      // Recursive search of the new variable found
      FL->resolveFieldsDefInBaseRecord(Componentlabel, FL);
      NBits -= VarSize;
    } else if (BtI) {
      // let's try to see if there are other constant bits behind the current
      // one for the same reason, varsize >= 1, preventing us to be in an
      // infinite loop
      unsigned BitFieldSize =
          CodeGenFormat::getFixedBits(Componentlabel, BI, NBits);

      BaseFieldPtr->addSubField(std::make_shared<TGFieldLayout>(
          CGI, Componentlabel, BaseFieldPtr, BitFieldSize,
          TGFieldLayout::FieldType::FixedBits));
      NBits -= BitFieldSize;
    } else {
      NBits--;
    }
  }
}

void TGFieldLayout::resolveFieldsDefInHierarchy(
    const std::string &LabelToFind, unsigned HierarchyLevel,
    const TGFieldLayoutPtr &BaseFieldPtr) {
  const Record *const BaseRecord = CGI->TheDef;

  // NOTE (Haydn port): upstream Record::getSuperClasses returns
  // std::vector<const Record *> (post-order); AIE returned
  // ArrayRef<pair<const Record*, SMRange>>. SMRange carried only source loc;
  // the post-order traversal order is identical. Adapted to the vector form.
  std::vector<const Record *> Hierarchy = BaseRecord->getSuperClasses();
  const BitsInit *BI = nullptr;

  // if we are out of range in the Hierarchy, stop recursion...
  if (HierarchyLevel >= Hierarchy.size()) {
    // ...but before, let's check if the label is defined in the base record
    // as it could be the case.
    // For example:
    // def I64_ST_LNG : AIE_i64_mv<(ins st_slot:$st, lng_slot:$lng),
    //                             "i64_st_lng", "$st, $lng"> {
    //    bits<20> st;
    //    bits<28> lng;
    //    let alu_st = {0b1, st};
    //    let mv_all = {0b000, lng};
    // }
    // Here, the labels "alu_st" and "mv_all" are only defined in the base
    // record I64_ST_LNG
    resolveFieldsDefInBaseRecord(LabelToFind, BaseFieldPtr);
    return;
  }
  const Record *CurrentLevelRecord = Hierarchy[HierarchyLevel];
  // if the label we are looking for isn't defined at this HierarchyLevel
  if (!CurrentLevelRecord->getValue(LabelToFind)) {
    // let's try on the upper level (if it is not defined there, the recursion
    // will be stopped by the first if statement)
    resolveFieldsDefInHierarchy(LabelToFind, HierarchyLevel + 1, BaseFieldPtr);
    return;
  }
  // Otherwise, the label is defined but it may not provide additionnal
  // information
  BI = CurrentLevelRecord->getValueAsBitsInit(LabelToFind);

  // If the field is defined but gives not extra information in the current
  // level
  if (areAllBitsNotComplete(BI)) {
    // then stop the execution of this one and request the same thing to the
    // upper level
    resolveFieldsDefInHierarchy(LabelToFind, HierarchyLevel + 1, BaseFieldPtr);
    return;
  }

  // areAllBitsNotComplete(BI) returned false
  // => The field we're looking for is defined, at least partially, at this
  // level
  DefRecord = CurrentLevelRecord;

  const VarBitInit *VBI = nullptr;
  const VarInit *VI = nullptr;
  const BitInit *BtI = nullptr;

  for (int NBits = BI->getNumBits() - 1; NBits >= 0;) {
    VBI = nullptr;
    VI = nullptr;
    BtI = nullptr;
    std::string Componentlabel;

    if ((VBI = dyn_cast<VarBitInit>(BI->getBit(NBits)))) {
      VI = dyn_cast<VarInit>(VBI->getBitVar());
      if (VI)
        Componentlabel = VI->getName().str();
    } else {
      BtI = dyn_cast<BitInit>(BI->getBit(NBits));
    }

    if (VI) {
      // See if there are other "variable" bits to group in the current
      // Variable.
      // NOTE: getVariableBits should always return >= 0 value in this context
      // as we know that there is at least one bit inside so varsize >= 1
      unsigned VarSize =
          CodeGenFormat::getVariableBits(Componentlabel, BI, NBits);

      std::shared_ptr<TGFieldLayout> const &FL =
          BaseFieldPtr->addSubField(std::make_shared<TGFieldLayout>(
              CGI, Componentlabel, BaseFieldPtr, VarSize,
              TGFieldLayout::FieldType::Variable));

      // Recursive search of the new variable found
      FL->resolveFieldsDefInHierarchy(Componentlabel, HierarchyLevel + 1, FL);
      NBits -= VarSize;
    } else if (BtI) {
      // let's try to see if there are other constant bits behind the current
      // one for the same reason, varsize >= 1, preventing us to be in an
      // infinite loop
      unsigned BitFieldSize =
          CodeGenFormat::getFixedBits(Componentlabel, BI, NBits);

      BaseFieldPtr->addSubField(std::make_shared<TGFieldLayout>(
          CGI, Componentlabel, BaseFieldPtr, BitFieldSize,
          TGFieldLayout::FieldType::FixedBits));
      NBits -= BitFieldSize;
    } else {
      NBits--;
    }
  }
}

void TGFieldLayout::resolveFieldsGlobalOffsets() {
  // GlobalOffsets are the positions of a given field in the global scope
  // (full encoding scope). This scope is bootstrapped by the Inst field,
  // which is the base field to override when defining a specific encoding
  // in TableGen.
  //
  // NOTE: we could performed the Global Offset resolution when building
  // the field tree (in resolveFieldsDef()) but it will add more complexity
  // into the recursive descent function. Thus, it has been prefered of
  // letting this detail on the side and dedicate another pass on this task.

  // We avoid re-computing the same nodes
  // Every nodes of the tree are resolved only once
  if (Parent && !Parent->isGlobalPositionKnown())
    Parent->resolveFieldsGlobalOffsets();

  // If it has a parent, compute the left GlobalOffsets based on the parent
  // left global offset and the (local) relative offset of the child in the
  // parent
  if (Parent)
    GlobalOffsets.LeftOffset = Parent->GlobalOffsets.LeftOffset +
                               Parent->computeRelativeChildOffset(this);
  // Otherwise, it must be the root of the tree where the left Offset is always
  // 0
  else
    GlobalOffsets.LeftOffset = 0;

  GlobalOffsets.RightOffset = GlobalOffsets.LeftOffset + this->getSize() - 1;
}

bool TGTargetSlots::addSlot(const Record *const R) {
  // If the instance is already finalized
  if (IsFinalized) {
    errs() << "Trying to add the slot " << R->getName()
           << " in a slot pool already finalized\n";
    exit(EXIT_FAILURE);
  }

  // Make sure the current Record is deriving from the Base Slot class
  if (!R->hasDirectSuperClass(BaseSlotClass))
    return false;

  // Instanciate the slot based on the TableGen Record
  TGTargetSlot CurrentSlot(R);

  // Check the current SlotName is unique, i.e. we should have no
  // entries in the Slots container having the same SlotName.
  // NOTE: this verification prevents us also to have multiples
  // same enumeration strings
  TGTargetSlots::const_iterator DuplicateSlot =
      findBySlotName(CurrentSlot.SlotName);

  if (DuplicateSlot != Slots.end()) {
    errs() << "Record Slot " << CurrentSlot.getInstanceName() << " and Slot "
           << DuplicateSlot->second.getInstanceName()
           << " have the same slot name (" << CurrentSlot.SlotName << ")\n"
           << "Use a different SlotName to solve this ambiguous case.\n";
    exit(EXIT_FAILURE);
  }

  // If we're adding a default slot but we already have one => error
  if (CurrentSlot.isDefaultSlot() && getDefaultSlot()) {
    errs() << "Multiples default slots defined: \n"
           << "- " << getDefaultSlot()->second.getInstanceName() << '\n'
           << "- " << CurrentSlot.getInstanceName() << '\n'
           << "One default slot (at most) must be defined\n";
    exit(EXIT_FAILURE);
  }

  const std::string SlotNamespace = CurrentSlot.getNamespace();

  if (SlotNamespace != Target) {
    dbgs() << "Conflicting namespaces between the Target (" << Target
           << ") and the current Slot (" << SlotNamespace << ").\n";
    dbgs() << "The slot (" << CurrentSlot.getInstanceName()
           << ") will not be added to the current Pool.";
    return false;
  }

  // let's find if the record is already in the Slots container
  // (integrity check). Normally, this won't happened.
  TGTargetSlots::const_iterator SlotPtr = find(R);

  // If it's the case, abort the adding procedure
  if (SlotPtr != Slots.end()) {
    errs() << "Record " << R->getName() << " already registered in the pool\n";
    exit(EXIT_FAILURE);
  }

  // If it's not the case, append the Pair in it
  Slots.push_back({R, CurrentSlot});
  return true;
}

void TGTargetSlots::finalizeSlots() {
  // Check if the default Slot exists.
  // Otherwise, create one.
  // NOTE: it is not mandatory to have a default slot defined, an artificial
  // one will be created in that case (in order to uniformize the slot logic)
  if (getDefaultSlot() == nullptr) {
    TGTargetSlot DefaultSlot(Target);
    // check if no other valid slot is labeled as the name "default"
    TGTargetSlots::const_iterator SlotPtr =
        findBySlotName(DefaultSlot.SlotName);
    // if this is not the case
    if (SlotPtr == Slots.end())
      // We're adding an extra entry in the Slots container.
      // NOTE: it's safe to add a nullptr reference here as the default slot
      // record will never be dereferenced.
      Slots.push_back({nullptr, DefaultSlot});
    else {
      errs() << "Valid slot (" << SlotPtr->second.getInstanceName()
             << ") already has \"unknown\" as SlotName: put this one as"
                " default or write explicitely another default slot record.";
      exit(EXIT_FAILURE);
    }
  }

  // Give an ID for each slot
  int SlotID = 0;
  for (auto &[_, Slot] : Slots)
    if (!Slot.isDefaultSlot())
      Slot.setNumSlot(SlotID++);

  // Sort the slot container by ID
  std::sort(Slots.begin(), Slots.end(),
            [](const RecordSlot &Rs0, const RecordSlot &Rs1) {
              return Rs0.second.getNumSlot() < Rs1.second.getNumSlot();
            });

  IsFinalized = true;
}

void TGTargetSlots::emitTargetSlotMapping(raw_ostream &o) const {

  o << "const MCSlotInfo *" << Target << "MCFormats::getSlotInfo";
  o << "(const MCSlotKind Kind) const {\n";

  const std::string EnumCstPreamble = "MC" + GenSlotKindName + "::";
  int Size = 0;
  for (const RecordSlot &Slot : Slots) {
    const TGTargetSlot &TS = Slot.second;
    // Default slot is not in the table
    if (TS.isDefaultSlot())
      continue;
    Size++;
  }
  o << "\tif (Kind < 0 || Kind >= " << Size << ") {\n"
    << "\t\treturn nullptr;\n"
    << "\t}\n"
    << "\treturn &" << Target << "Slots[Kind];\n"
    << "}\n";
}

void TGTargetSlots::emitSlotsInfoInstantiation(
    raw_ostream &o, const TGInstrLayout::NOPSlotMap &SlotMapper) const {
  assert(IsFinalized && "Internal vector needs to be finalized (i.e. sorted)");

  const std::string TargetEnumName = "MC" + GenSlotKindName;
  const std::string TargetSlotsName = Target + "Slots";

  o << "static constexpr const MCSlotInfo " << TargetSlotsName << "[] = {\n";

  for (const RecordSlot &Slot : Slots) {
    auto SlotMap = SlotMapper.find(Slot.first);
    const TGTargetSlot &TS = Slot.second;
    // Skip the emission of the default slot
    if (TS.isDefaultSlot())
      continue;

    std::string NOPName =
        SlotMap == SlotMapper.end() ? "0" : Target + "::" + SlotMap->second;

    o << "{\n"
      << "  \"" << TS.getSlotName() << "\",\n"
      << "  " << TS.getSlotSize()
      << ",\n"
      // Right now, we're using the slot num as SlotSet
      << "  " << TS.getSlotBits() << ",\n"
      << "  " << TS.getConflictBits() << ",\n"
      << "  " << NOPName << "\n"
      << "},\n";
  }
  o << "};\n";
}

TGFieldIterator::TGFieldIterator(
    TGFieldLayout *InitField, Mode Mode,
    const std::function<bool(const TGFieldLayout &)> &Predicate)
    : TraversalMode(Mode), FunctorPolicy(Predicate) {
  bool HasToStop = false;

  if (InitField) {
    InternalStack.push(InitField);
    do {
      HasToStop = !iterateOverFields();
      if (HasToStop)
        break;
    } while (!InternalStack.empty());
  }

  if (!HasToStop && InternalStack.empty())
    FieldCurrent = nullptr;
}

inline bool TGFieldIterator::iterateOverFields() {
  FieldCurrent = InternalStack.top();
  InternalStack.pop();

  // If we are using the FullTraversal mode or the predicate is false
  if (TraversalMode == Mode::FullTraversal || (!FunctorPolicy(*FieldCurrent)))
    // Load all the subfields in the reverse order as we're storing them into
    // a stack (LIFO)
    for (reverse_iterator it = FieldCurrent->LayoutEntries.rbegin();
         it != FieldCurrent->LayoutEntries.rend(); ++it)
      InternalStack.push(it->get());
  // If the functor returns true, then we have to stop
  // The iterator will then point on this first element
  return !FunctorPolicy(*FieldCurrent);
}

TGFieldIterator &TGFieldIterator::operator++() {
  bool HasToStop = false;

  // If FieldCurrent is the last field
  if (FieldCurrent && InternalStack.empty())
    // then put it at null
    FieldCurrent = nullptr;

  while (!InternalStack.empty()) {
    HasToStop = !iterateOverFields();
    if (HasToStop)
      break;
  }

  // if the stack is now empty but no valid element has been found
  if (!HasToStop && InternalStack.empty())
    FieldCurrent = nullptr;

  return *this;
}

TGFieldLayout *TGFieldIterator::operator*() const { return FieldCurrent; }

bool TGFieldIterator::operator==(const TGFieldIterator &Other) const {
  return FieldCurrent == Other.FieldCurrent;
}

static TableGen::Emitter::OptClass<CodeGenFormat>
    X("gen-instr-format", "Instruction Format Emitter");
