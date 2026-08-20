//===-- HaydnGenFormatEInverse.inc - MemberId inverse index -*- C++ -*-===//
//
// FormatEInverse in HaydnGenFormatERecords.inc is independently sorted by
// (mode, entry, unit, type, opcode) — not by MemberId. FormatEInverse[I]
// is therefore not the inverse of FormatEMembers[I]. This index is the
// verifier's MemberId view of those separately generated inverse records.
// HaydnBundleVerify consumes this index plus FormatEInverse only — never
// FormatEMembers as an inverse row, never Bundle.canAdd / DFS / name peel.
// Residual expand-owned and cycle-forming pseudos are not inverse keys.
//
// Peer: inverse of AIEMCFormats::getAlternateInstsOpcode
// (AIEMCFormats.h:376-379; CodeGenFormat.cpp:155-163). Overlay is the
// generated FormatEInverse table keyed by MemberId, not a sibling planner.
//
//===----------------------------------------------------------------------===//

#ifdef GET_FORMAT_E_INVERSE_INDEX
#undef GET_FORMAT_E_INVERSE_INDEX

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
namespace haydn {
namespace format_e {

/// MemberId → FormatEInverse row index. ~0u when the inverse table has no
/// record for \p MemberId (generator pin: every member has exactly one).
inline unsigned inverseIndexForMemberId(unsigned MemberId) {
  static const unsigned *const ById = [] {
    static unsigned Idx[FormatEMemberCount];
    for (unsigned I = 0; I < FormatEMemberCount; ++I)
      Idx[I] = ~0u;
    for (unsigned I = 0; I < FormatEMemberCount; ++I) {
      const unsigned Mid = FormatEInverse[I].MemberId;
      if (Mid < FormatEMemberCount)
        Idx[Mid] = I;
    }
    return Idx;
  }();
  if (MemberId >= FormatEMemberCount)
    return ~0u;
  return ById[MemberId];
}

/// MemberId → separately generated FormatEInverse record. Null when the
/// inverse table has no row for \p MemberId.
inline const FormatEInverseRec *inverseRecordForMemberId(unsigned MemberId) {
  const unsigned I = inverseIndexForMemberId(MemberId);
  if (I >= FormatEMemberCount)
    return nullptr;
  return &FormatEInverse[I];
}

/// Every FormatEInverse row whose catalog Logical equals \p Logical.
/// Public mnemonics (LD32) resolve to this span via a member's Logical,
/// never by peeling the mnemonic onto the catalog name.
inline void inverseIdsForLogical(StringRef Logical,
                                 SmallVectorImpl<unsigned> &Ids) {
  static const StringMap<SmallVector<unsigned, 8>> Map = [] {
    StringMap<SmallVector<unsigned, 8>> M;
    for (unsigned I = 0; I < FormatEMemberCount; ++I) {
      const FormatEInverseRec &R = FormatEInverse[I];
      if (!R.Logical || StringRef(R.Logical).equals_insensitive("NOP"))
        continue;
      M[R.Logical].push_back(I);
    }
    return M;
  }();
  if (Logical.empty() || Logical.equals_insensitive("NOP"))
    return;
  auto It = Map.find(Logical);
  if (It == Map.end())
    return;
  Ids.append(It->second.begin(), It->second.end());
}

/// Membership + encodeability for one independently generated inverse row.
/// Unit injectivity is a multi-member property (verifyCommittedBundle).
/// Placement key reconstructs MemberId via findInverseMemberId (sorted
/// inverse table). FormatEMembers is the membership/encodeability witness
/// for that row — never an inverse selection source (no UnitMap / name peel).
/// Peer: AIE getAlternateInstsOpcode returns generated table entries
/// (CodeGenFormat.cpp:155-163; AIEMCFormats.h:376-379).
inline bool completeInverseRecord(const FormatEInverseRec &R) {
  if (R.MemberId >= FormatEMemberCount || !R.Logical)
    return false;
  if (StringRef(R.Logical).equals_insensitive("NOP"))
    return false;
  if (!R.TypeName || StringRef(R.TypeName).empty())
    return false;
  const FormatEMemberRec &M = FormatEMembers[R.MemberId];
  if (M.IsNop || M.MemberId != R.MemberId)
    return false;
  if (M.Mode != R.Mode || M.EntryIdx != R.EntryIdx || M.Unit != R.Unit ||
      M.TypeCode != R.TypeCode || M.Opcode != R.Opcode)
    return false;
  if (!M.Logical || StringRef(M.Logical) != StringRef(R.Logical))
    return false;
  const FormatEInverseRec *ById = inverseRecordForMemberId(R.MemberId);
  if (!ById || ById->MemberId != R.MemberId)
    return false;
  if (ById->Mode != R.Mode || ById->EntryIdx != R.EntryIdx ||
      ById->Unit != R.Unit || ById->TypeCode != R.TypeCode ||
      ById->Opcode != R.Opcode)
    return false;
  const int Hit =
      findInverseMemberId(R.Mode, R.EntryIdx, R.Unit, R.TypeCode, R.Opcode);
  return Hit >= 0 && static_cast<unsigned>(Hit) == R.MemberId;
}

} // namespace format_e
} // namespace haydn
} // namespace llvm

#endif // GET_FORMAT_E_INVERSE_INDEX
