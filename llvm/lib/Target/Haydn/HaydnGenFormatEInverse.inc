//===-- HaydnGenFormatEInverse.inc - MemberId inverse index -*- C++ -*-===//
//
// FormatEInverse in HaydnGenFormatERecords.inc is independently sorted by
// (mode, entry, unit, type, opcode) — not by MemberId. FormatEInverse[I]
// is therefore not the inverse of FormatEMembers[I]. This index is the
// verifier's MemberId view of those separately generated inverse records.
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

} // namespace format_e
} // namespace haydn
} // namespace llvm

#endif // GET_FORMAT_E_INVERSE_INDEX
