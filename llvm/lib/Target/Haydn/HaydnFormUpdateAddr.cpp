//===-- HaydnFormUpdateAddr.cpp - RETIRED ---------------------------------===//
//
// FormUpdateAddr (post-ISel MIR fold of LD/ST+ADD → AGU writeback) is retired.
// Product AGU pre/post-inc form is GISel-only (HaydnPostLegalizerCombiner +
// -haydn-enable-gisel-update-addr). Dual fuse paths diverged; do not revive.
//
// This translation unit intentionally has no symbols.
//
//===----------------------------------------------------------------------===//
