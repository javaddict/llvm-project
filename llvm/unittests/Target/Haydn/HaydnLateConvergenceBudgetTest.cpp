//===- HaydnLateConvergenceBudgetTest.cpp - GR2.6 closure law tests -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: LLVM-exception.
//
//===----------------------------------------------------------------------===//
//
// GR2.6 / D1.41: unit checks for the exposed budget-law types and the
// exact-attribution no-growth predicate (haydnClosureGrowthAccount), plus
// a parity pin that the shared still-relaxable classifier is the ONE law
// (constants match the Fixup margin vocabulary).
//
// The accounting law is pure: it needs no MachineFunction. Illegal growth
// has no healthy-compiler producer, so the firing path is proven here
// red/green plus a source pin in late-convergence-o0.ll (EXHAUST
// precedent). D1.41 adds the exact-accounting arms: an event on one span
// never grants credit to an unrelated prefix (red), multi-event one-span
// accounting, and stall credit bounded by the parcels actually inserted.
//
//===----------------------------------------------------------------------===//

#include "HaydnHWLoopContracts.h"
#include "HaydnLateConvergence.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "gtest/gtest.h"

using namespace llvm;
using haydn::hwloop::MaxHwLoopDemoteGrowthBytes;
using haydn::hwloop::MaxHwLoopDemoteGrowthParcels;
using haydn::hwloop::MaxSingleBranchGrowthBytes;
using haydn::hwloop::MaxSingleBranchGrowthParcels;
using haydn::hwloop::ProductParcelBytes;

namespace {

HaydnPrefixPairRecord makePair(unsigned Src, unsigned Dst, uint64_t Encoded,
                               uint64_t Pad, unsigned Sites = 0,
                               unsigned Setups = 0) {
  HaydnPrefixPairRecord P;
  P.Src = Src;
  P.Dst = Dst;
  P.EncodedBytes = Encoded;
  P.AlignPad = Pad;
  P.MaxAlign = Align(1);
  P.RelaxableSites = Sites;
  P.HwLoopSetups = Setups;
  return P;
}

// Event factory: one attributed event of class K with exact Bytes on the
// named MBBs.
HaydnClosureGrowthEvent makeEvent(HaydnClosureGrowthEvent::Kind K,
                                  uint64_t Bytes,
                                  std::initializer_list<unsigned> MBBs) {
  SmallVector<unsigned, 2> Affected(MBBs);
  return HaydnClosureGrowthEvent(K, Bytes, std::move(Affected));
}

// Shorthand accounting call for the common no-extra-allowance case.
static std::string account(const HaydnPrefixBudgetRecord &Before,
                           const HaydnPrefixBudgetRecord &After,
                           const HaydnClosureEventLedger &Ledger) {
  return haydnClosureGrowthAccount(Before, After, Ledger,
                                   MaxSingleBranchGrowthBytes,
                                   MaxHwLoopDemoteGrowthBytes);
}

// No events, no growth: green.
TEST(HaydnLateConvergenceBudgetTest, NoGrowthNoEventIsAccounted) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12));
  After.Pairs.push_back(makePair(1, 5, 100, 12));
  HaydnClosureEventLedger Ledger; // no events
  EXPECT_TRUE(account(Before, After, Ledger).empty());
}

// Shrink is fine even with no events.
TEST(HaydnLateConvergenceBudgetTest, ShrinkIsAlwaysAccounted) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12));
  After.Pairs.push_back(makePair(1, 5, 40, 0));
  HaydnClosureEventLedger Ledger;
  EXPECT_TRUE(account(Before, After, Ledger).empty());
}

// Unaccounted growth with an empty ledger is the fatal (red).
TEST(HaydnLateConvergenceBudgetTest, GrowthWithoutEventIsUnaccounted) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12));
  After.Pairs.push_back(makePair(1, 5, 100 + 13, 12));
  HaydnClosureEventLedger Ledger; // no events
  const std::string Diag = account(Before, After, Ledger);
  EXPECT_FALSE(Diag.empty());
  EXPECT_NE(Diag.find("grew"), std::string::npos);
}

// Promotion within MaxSingleBranchGrowthBytes on a span that had a
// relaxable site is admitted (green).
TEST(HaydnLateConvergenceBudgetTest, PromotionWithinBudgetAdmitted) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12, /*Sites=*/1));
  After.Pairs.push_back(makePair(1, 5, 100 + MaxSingleBranchGrowthBytes, 12,
                                 /*Sites=*/0));
  HaydnClosureEventLedger Ledger;
  Ledger.Events.push_back(makeEvent(
      HaydnClosureGrowthEvent::Kind::Promotion, MaxSingleBranchGrowthBytes,
      /*MBBs=*/{3}));
  EXPECT_TRUE(account(Before, After, Ledger).empty());

  // One byte past the budget is unaccounted.
  After.Pairs[0].EncodedBytes = 100 + MaxSingleBranchGrowthBytes + 1;
  EXPECT_FALSE(account(Before, After, Ledger).empty());
}

// A promotion with NO relaxable site in the span is unaccounted (the
// budget only admits growth over sites the classifier counted).
TEST(HaydnLateConvergenceBudgetTest, PromotionWithoutSiteUnaccounted) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12, /*Sites=*/0));
  After.Pairs.push_back(makePair(1, 5, 100 + MaxSingleBranchGrowthBytes, 12));
  HaydnClosureEventLedger Ledger;
  Ledger.Events.push_back(makeEvent(
      HaydnClosureGrowthEvent::Kind::Promotion, MaxSingleBranchGrowthBytes,
      /*MBBs=*/{3}));
  EXPECT_FALSE(account(Before, After, Ledger).empty());
}

// Two promotion events admitted only while the span held >= 2 sites.
TEST(HaydnLateConvergenceBudgetTest, MultiSitePromotionCap) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12, /*Sites=*/1));
  After.Pairs.push_back(
      makePair(1, 5, 100 + 2 * MaxSingleBranchGrowthBytes, 12));
  HaydnClosureEventLedger Ledger;
  Ledger.Events.push_back(makeEvent(
      HaydnClosureGrowthEvent::Kind::Promotion, MaxSingleBranchGrowthBytes,
      /*MBBs=*/{2}));
  Ledger.Events.push_back(makeEvent(
      HaydnClosureGrowthEvent::Kind::Promotion, MaxSingleBranchGrowthBytes,
      /*MBBs=*/{4}));
  EXPECT_FALSE(account(Before, After, Ledger).empty()); // only 1 site
}

// Demote insertion within MaxHwLoopDemoteGrowthBytes per lost setup.
TEST(HaydnLateConvergenceBudgetTest, DemoteWithinBudgetAdmitted) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12, /*Sites=*/0, /*Setups=*/1));
  After.Pairs.push_back(
      makePair(1, 5, 100 + MaxHwLoopDemoteGrowthBytes, 12));
  HaydnClosureEventLedger Ledger;
  Ledger.Events.push_back(makeEvent(
      HaydnClosureGrowthEvent::Kind::Demote, MaxHwLoopDemoteGrowthBytes,
      /*MBBs=*/{3}));
  EXPECT_TRUE(account(Before, After, Ledger).empty());

  // Past the demote bound is unaccounted.
  After.Pairs[0].EncodedBytes = 100 + MaxHwLoopDemoteGrowthBytes + 1;
  EXPECT_FALSE(account(Before, After, Ledger).empty());
}

// D1.41: a demote event measured on an MBB whose measured bytes EXCEED the
// vocabulary bound admits only up to the bound — the cap is the law, the
// measurement is the credit.
TEST(HaydnLateConvergenceBudgetTest, DemoteMeasurementCappedByVocabulary) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12, /*Sites=*/0, /*Setups=*/1));
  After.Pairs.push_back(
      makePair(1, 5, 100 + MaxHwLoopDemoteGrowthBytes + 12, 12));
  HaydnClosureEventLedger Ledger;
  // Over-measured single demote: the exact bytes exceed the bound.
  Ledger.Events.push_back(makeEvent(
      HaydnClosureGrowthEvent::Kind::Demote,
      MaxHwLoopDemoteGrowthBytes + 12, /*MBBs=*/{3}));
  EXPECT_FALSE(account(Before, After, Ledger).empty());
}

// Stall insertion admitted only when a stall event is attributed to the
// span, and then ONLY the exact inserted bytes (D1.41: no fixed
// allowance — the pre-D1.41 boolean granted StallAllowanceBytes to every
// span).
TEST(HaydnLateConvergenceBudgetTest, StallExactBytesOnly) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12));
  HaydnClosureEventLedger Ledger;

  // Exact 24-byte stall parcel insertion on an interior MBB: admitted.
  After.Pairs.push_back(makePair(1, 5, 100 + 24, 12));
  Ledger.Events.push_back(
      makeEvent(HaydnClosureGrowthEvent::Kind::Stall, 24, /*MBBs=*/{3}));
  EXPECT_TRUE(account(Before, After, Ledger).empty());

  // One byte past the exact inserted bytes: unaccounted (the exact bytes
  // are the whole credit — no allowance beyond them).
  After.Pairs[0].EncodedBytes = 100 + 24 + 1;
  EXPECT_FALSE(account(Before, After, Ledger).empty());

  // No stall event at all: the same 24 bytes are unaccounted.
  After.Pairs[0].EncodedBytes = 100 + 24;
  Ledger.Events.clear();
  EXPECT_FALSE(account(Before, After, Ledger).empty());
}

// D1.41 red-then-green core: an event fired on span A admits NOTHING for
// an unrelated span B that grew in the same iteration. Pre-D1.41 the
// boolean StallsChanged granted B the fixed allowance and B grew silently.
TEST(HaydnLateConvergenceBudgetTest, UnrelatedPrefixRefused) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12));
  Before.Pairs.push_back(makePair(20, 26, 100, 12));
  After.Pairs.push_back(makePair(1, 5, 100 + 24, 12)); // stall hit here
  After.Pairs.push_back(makePair(20, 26, 100 + 24, 12)); // unrelated growth
  HaydnClosureEventLedger Ledger;
  Ledger.Events.push_back(
      makeEvent(HaydnClosureGrowthEvent::Kind::Stall, 24, /*MBBs=*/{3}));
  const std::string Diag = account(Before, After, Ledger);
  EXPECT_FALSE(Diag.empty());
  EXPECT_NE(Diag.find("bb.20->bb.26"), std::string::npos);
  EXPECT_NE(Diag.find("grew"), std::string::npos);

  // Green shape: the unrelated prefix does not grow.
  After.Pairs[1].EncodedBytes = 100;
  EXPECT_TRUE(account(Before, After, Ledger).empty());
}

// D1.41 multi-event one-span: two stall insertions on two MBBs of ONE
// span are both admitted exactly; a third insertion claimed on the span
// without an event is refused.
TEST(HaydnLateConvergenceBudgetTest, MultiEventOneSpan) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 9, 100, 12));
  HaydnClosureEventLedger Ledger;
  Ledger.Events.push_back(
      makeEvent(HaydnClosureGrowthEvent::Kind::Stall, 24, /*MBBs=*/{2}));
  Ledger.Events.push_back(
      makeEvent(HaydnClosureGrowthEvent::Kind::Stall, 12, /*MBBs=*/{6}));

  // Both insertions, exact sum: admitted.
  After.Pairs.push_back(makePair(1, 9, 100 + 24 + 12, 12));
  EXPECT_TRUE(account(Before, After, Ledger).empty());

  // Growth beyond the exact sum of the two events: unaccounted.
  After.Pairs[0].EncodedBytes = 100 + 24 + 12 + 1;
  EXPECT_FALSE(account(Before, After, Ledger).empty());
}

// D1.41 span-membership law: events outside the numeric span interval do
// not credit it, events on the endpoints do (the span charge includes the
// Src block's own bytes; the Dst block terminates the walk).
TEST(HaydnLateConvergenceBudgetTest, SpanMembershipLaw) {
  EXPECT_TRUE(haydnSpanContainsMBB(1, 5, 1));
  EXPECT_TRUE(haydnSpanContainsMBB(1, 5, 3));
  EXPECT_FALSE(haydnSpanContainsMBB(1, 5, 6));
  EXPECT_FALSE(haydnSpanContainsMBB(1, 5, 0));
  // Backedge span (Dst < Src): numeric interior, both directions.
  EXPECT_TRUE(haydnSpanContainsMBB(7, 2, 5));
  EXPECT_FALSE(haydnSpanContainsMBB(7, 2, 8));
  // Self-pair (latch): exactly the one block.
  EXPECT_TRUE(haydnSpanContainsMBB(4, 4, 4));
  EXPECT_FALSE(haydnSpanContainsMBB(4, 4, 5));
}

// Stall regen bounded by actual inserted stalls: an MBB that SHRANK
// (stripped regenerable parcels) earns no credit, and its shrinkage may
// not subsidize growth claimed elsewhere in the same span.
TEST(HaydnLateConvergenceBudgetTest, StallRegenBoundedByInserted) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 9, 100, 12));
  HaydnClosureEventLedger Ledger;
  // bb.2 regen: stripped 24, re-inserted 36 → net +12 (exact credit 12).
  // bb.6: pure shrink −12 (no event, no credit).
  Ledger.Events.push_back(
      makeEvent(HaydnClosureGrowthEvent::Kind::Stall, 12, /*MBBs=*/{2}));

  // Span grows exactly the net insert: admitted.
  After.Pairs.push_back(makePair(1, 9, 100 + 12, 12));
  EXPECT_TRUE(account(Before, After, Ledger).empty());

  // Span grows net+1 while another MBB shrank: the shrinkage is NOT
  // credit; unaccounted.
  After.Pairs[0].EncodedBytes = 100 + 13;
  EXPECT_FALSE(account(Before, After, Ledger).empty());
}

// Vanished key without split evidence is unaccounted; with MBB growth it
// is split-evidenced and admitted. (A pure renumber both vanishes and
// appears keys; the appeared check fires first there — the pure-vanished
// shape below isolates the vanished diagnostic.)
TEST(HaydnLateConvergenceBudgetTest, VanishedKeyNeedsSplitEvidence) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12));
  Before.Pairs.push_back(makePair(2, 7, 100, 12));
  After.Pairs.push_back(makePair(1, 5, 100, 12)); // (2,7) silently gone
  HaydnClosureEventLedger Ledger;                 // no MBB growth
  const std::string Diag = account(Before, After, Ledger);
  EXPECT_FALSE(Diag.empty());
  EXPECT_NE(Diag.find("vanished"), std::string::npos);

  Ledger.MBBGrowth = 1; // BranchRelaxation split evidence
  EXPECT_TRUE(account(Before, After, Ledger).empty());
}

// A renumbered pair (old key vanished, new key appeared) with an empty
// ledger is admitted ONLY as a balanced pure retarget (appeared charge <=
// vanished charge — BranchRelaxation re-relaxing a cond whose far arm
// already runs through a trampoline); a renumber that GROWS the charge
// with no event stays unaccounted.
TEST(HaydnLateConvergenceBudgetTest, RenumberedPairBalancedRetargetLaw) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12));
  After.Pairs.push_back(makePair(1, 6, 100, 12)); // equal charge: retarget
  HaydnClosureEventLedger Ledger;                 // no events at all
  EXPECT_TRUE(account(Before, After, Ledger).empty());

  // Retarget that grows the charge is NOT balanced: unaccounted.
  After.Pairs[0].EncodedBytes = 200;
  EXPECT_FALSE(account(Before, After, Ledger).empty());

  // Split evidence admits the growing renumber (block-boundary movement).
  Ledger.MBBGrowth = 1;
  EXPECT_TRUE(account(Before, After, Ledger).empty());
}

// Renumber rotation on a stalls iteration: every key rotates (empty-hole
// compaction) while a stall event grows one span. The rotation is ONE
// aggregate span — total appeared within total vanished plus the exact
// stall bytes attributed to the vanished keys' spans.
TEST(HaydnLateConvergenceBudgetTest, RenumberRotationWithStallGrowth) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(2, 3, 36, 0));
  Before.Pairs.push_back(makePair(8, 10, 48, 0));
  After.Pairs.push_back(makePair(2, 4, 36, 0));
  After.Pairs.push_back(makePair(11, 14, 60, 0)); // +12 stall parcel
  HaydnClosureEventLedger Ledger;
  // The stall event lived inside the pre-rotation (8,10) span.
  Ledger.Events.push_back(
      makeEvent(HaydnClosureGrowthEvent::Kind::Stall, 12, /*MBBs=*/{9}));
  EXPECT_TRUE(account(Before, After, Ledger).empty());

  // One byte past the exact aggregate credit is unaccounted.
  After.Pairs[1].EncodedBytes += 1;
  EXPECT_FALSE(account(Before, After, Ledger).empty());
}

// Appeared key without split evidence is unaccounted.
TEST(HaydnLateConvergenceBudgetTest, AppearedKeyNeedsSplitEvidence) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12));
  After.Pairs.push_back(makePair(1, 5, 100, 12));
  After.Pairs.push_back(makePair(2, 7, 100, 12)); // new key, no split
  HaydnClosureEventLedger Ledger;
  const std::string Diag = account(Before, After, Ledger);
  EXPECT_FALSE(Diag.empty());
  EXPECT_NE(Diag.find("appeared"), std::string::npos);

  Ledger.MBBGrowth = 1;
  EXPECT_TRUE(account(Before, After, Ledger).empty());
}

// Entry-vs-final telemetry law: strict no-growth over surviving keys.
TEST(HaydnLateConvergenceBudgetTest, NoGrowthTelemetry) {
  HaydnPrefixBudgetRecord Entry, Final;
  Entry.Pairs.push_back(makePair(1, 5, 100, 12));
  Final.Pairs.push_back(makePair(1, 5, 100, 12));
  EXPECT_TRUE(Entry.noGrowth(Final));

  Final.Pairs[0].EncodedBytes = 101;
  EXPECT_FALSE(Entry.noGrowth(Final));

  // Shrink is no-growth.
  Final.Pairs[0].EncodedBytes = 60;
  EXPECT_TRUE(Entry.noGrowth(Final));
}

// Parcel-rounded pad reserve: the reserve is never the raw A-1 value.
TEST(HaydnLateConvergenceBudgetTest, ParcelRoundedPadReserve) {
  // With product EncodedBytes=12: Align(16) raw worst case is 15; the
  // parcel-rounded reserve is ceilParcels(15)*12 = 24. The prefix record
  // is populated by the driver; here we pin the law through the constants
  // the reserve is built from (the arithmetic lives in the .cpp's
  // parcelRoundedUnknownPad; the invariant under test is that the budget
  // vocabulary never under-reserves on the 12-byte grid).
  const unsigned RawWorst = 16 - 1;
  const unsigned Parcels = (RawWorst + ProductParcelBytes - 1) /
                           static_cast<unsigned>(ProductParcelBytes);
  EXPECT_EQ(Parcels, 2u);
  EXPECT_EQ(haydn::bundle::productBundlesToBytes(Parcels), 24);
  EXPECT_GT(haydn::bundle::productBundlesToBytes(Parcels), RawWorst);
}

// Parity pin: the constants the budget accounting consumes are exactly the
// contracts vocabulary the Fixup margin law uses (one mechanism).
// MaxHwLoopDemoteGrowthParcels=13 static_asserts unchanged (D1.41 does
// NOT touch the bound: exact accounting needs no bound change).
TEST(HaydnLateConvergenceBudgetTest, SharedClassifierVocabularyParity) {
  EXPECT_EQ(MaxSingleBranchGrowthParcels, 4u);
  EXPECT_EQ(MaxSingleBranchGrowthBytes,
            haydn::bundle::productBundlesToBytes(
                MaxSingleBranchGrowthParcels));
  EXPECT_EQ(MaxHwLoopDemoteGrowthParcels, 13u);
  EXPECT_EQ(MaxHwLoopDemoteGrowthBytes,
            haydn::bundle::productBundlesToBytes(
                MaxHwLoopDemoteGrowthParcels));
  EXPECT_EQ(haydn::hwloop::BranchRelaxSafetyBufferBytes,
            MaxSingleBranchGrowthBytes);
  EXPECT_GT(MaxHwLoopDemoteGrowthBytes, MaxSingleBranchGrowthBytes);
}

// D1.41 fail-closed scope law: an event with NO scope (no exact pair, no
// MBBs) admits nothing — the pre-D1.41 whole-iteration grant is gone.
TEST(HaydnLateConvergenceBudgetTest, UnscopedEventAdmitsNothing) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12));
  Before.Pairs.push_back(makePair(20, 26, 100, 12));
  After.Pairs.push_back(makePair(1, 5, 100 + 12, 12));
  After.Pairs.push_back(makePair(20, 26, 100 + 12, 12));
  HaydnClosureEventLedger Ledger;
  Ledger.Events.push_back(
      makeEvent(HaydnClosureGrowthEvent::Kind::Stall, 12, /*MBBs=*/{}));
  EXPECT_FALSE(account(Before, After, Ledger).empty());
}

// D1.41 pair-scope exactness: a pair-scoped event credits ONLY its own
// key — the identical sibling key is an unrelated prefix and gets nothing.
TEST(HaydnLateConvergenceBudgetTest, PairScopeIsExact) {
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(1, 5, 100, 12));
  Before.Pairs.push_back(makePair(1, 6, 100, 12));
  After.Pairs.push_back(makePair(1, 5, 100 + 12, 12));
  After.Pairs.push_back(makePair(1, 6, 100 + 12, 12)); // sibling grew too
  HaydnClosureEventLedger Ledger;
  Ledger.Events.push_back(HaydnClosureGrowthEvent::forPair(
      HaydnClosureGrowthEvent::Kind::Stall, 12, 1, 5));
  const std::string Diag = account(Before, After, Ledger);
  EXPECT_FALSE(Diag.empty());
  EXPECT_NE(Diag.find("bb.1->bb.6"), std::string::npos);
}

// Fleet regression (cxfir16x16_hifi3): a LongBranchNormalize far-site
// promotion CONSUMES the promoted pair key — the LUI+ADDI+cond+JALR long
// form carries no range-pair MBB operand, so the key vanishes while the
// rewrite's own byte growth lands on the SURVIVING spans that grew (the
// latch self-prefix). The vanished key's migration evidence is a
// zero-byte Promotion event scoped to exactly that key: it admits the
// disappearance, credits no surviving span, and a vanished key WITHOUT it
// still fatals (fail-closed).
TEST(HaydnLateConvergenceBudgetTest, VanishedKeyByPromotionIsPairScoped) {
  // cxfir16x16 shape: iter-in held bb.6->bb.6 (324) and bb.6->bb.2 (1704);
  // the promotion rewrote the bb.6->bb.2 branch, so the key vanished and
  // the latch self-prefix grew 324 -> 348 (the exact inserted parcels).
  HaydnPrefixBudgetRecord Before, After;
  Before.Pairs.push_back(makePair(6, 6, 324, 0));
  Before.Pairs.push_back(makePair(6, 2, 1704, 0));
  After.Pairs.push_back(makePair(6, 6, 348, 0));
  HaydnClosureEventLedger Ledger;
  // The self-prefix growth: exact stall-class bytes on its own key.
  Ledger.Events.push_back(HaydnClosureGrowthEvent::forPair(
      HaydnClosureGrowthEvent::Kind::Stall, 24, 6, 6));
  // The consumed key: zero-byte promotion evidence scoped to itself.
  Ledger.Events.push_back(HaydnClosureGrowthEvent::forPair(
      HaydnClosureGrowthEvent::Kind::Promotion, 0, 6, 2));
  EXPECT_TRUE(account(Before, After, Ledger).empty());

  // Without the promotion event the same iteration is the fleet fatal:
  // "vanished pair bb.6->bb.2 with no split evidence".
  HaydnClosureEventLedger NoPromotion;
  NoPromotion.Events.push_back(HaydnClosureGrowthEvent::forPair(
      HaydnClosureGrowthEvent::Kind::Stall, 24, 6, 6));
  const std::string Diag = account(Before, After, NoPromotion);
  EXPECT_FALSE(Diag.empty());
  EXPECT_NE(Diag.find("vanished"), std::string::npos);
  EXPECT_NE(Diag.find("bb.6->bb.2"), std::string::npos);

  // The zero-byte promotion never credits a surviving span: growth on the
  // self-prefix beyond its own exact stall bytes stays unaccounted.
  After.Pairs[0].EncodedBytes = 348 + 1;
  EXPECT_FALSE(account(Before, After, Ledger).empty());
}

} // namespace
