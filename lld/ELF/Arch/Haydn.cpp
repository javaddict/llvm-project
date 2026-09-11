//===- Haydn.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn is a VLIW DSP architecture. This file handles ELF linking for Haydn
// targets (baremetal, 32-bit little-endian).
//
// Product encoding profile is Format E (sole active family): parcel size is
// the generated EncodedBytes from the object-encoding registry (HaydnFormat).
// Layout/veneers/padding geometry must not scatter bare parcel-width literals.
//
// Relocation overview:
//   Alignment, scale, and field range live only in HaydnRelocLayout
//   (computeRelocValue). Branch/call are byte PC+imm (ValueShift=0);
//   consumers must not invent a scale. Summary of common PC-rel kinds:
//   R_HAYDN_BranchSImm16 — signed branch field (range via HaydnRelocLayout).
//   R_HAYDN_CallSImm20 — signed call field (range via HaydnRelocLayout).
//   R_HAYDN_WIDE_BranchSImm12/_RI — narrow signed branch field.
//   R_HAYDN_WIDE_CallSImm20 — wide signed call field.
//   R_HAYDN_JALRSImm12 / _E3E0 / _E3E1 — residual ELF 22/32/33 identity
//     (do not remint). Execution is golden rs+imm12. Symbolic JALR has
//     no golden relocation base (ISA-69): getRelExpr/relocate fail closed
//     and return R_NONE; never silent R_PC. Literal jalr rd, rs, 0 does
//     not emit this kind.
//   R_HAYDN_CSR_UImm8 — Format E CSR I8 uimm8 (absolute unsigned CSR
//     address; reloc CSRW_W / CSRR). Not R_HAYDN_8 (data .byte).
//   R_HAYDN_HWLoopOff1/Off2 — unsigned Format E SET_HWLOOP displacement
//     fields (<<2 law; FieldLsb via HaydnRelocLayout / resolveFieldLsb).
//   Out-of-range branch/call sites fail closed with a D1.57 veneer-ABI
//   diagnostic (needsThunk arms addThunkHaydn; no Haydn thunk exists).
//   In-range returning calls (HI12+LO20+JALR rd=LR, rs=LUI.rd) may
//   rewrite the JALR parcel in place to JAL when CallSImm20 fits
//   (HaydnCallRelax). LUI+ADDI stay — they write the address temp, which
//   later jalr lr, temp (CoreMark iterate crc) still reads. Idling those
//   parcels is RISC-V AUIPC-delete and jumps later sites to stack.
//   Long jumps and mismatched JALR.rs stay JALR. Packet/cycle count
//   stays identical. Not a veneer.
//   R_HAYDN_HI20/LO16 — LUI+ADDI32 pair for 32-bit absolute addressing.
//   R_HAYDN_HI12 — LUI I12 high 12 (RelocTrans::Hi12; specifier %hi12).
//     Producer emission is D1.17 TYPED: non-default E3 sites arrive as the
//     R_HAYDN_HI12_E3* qualified kinds (typed window 21/23/54/83/81; no
//     sniff). The base kind keeps the E2 e0 @32 window plus the opc-pinned
//     sniff fallback (E3 e0 ALU2 @21 / ALU0 @23; e1 @54; e2 ALU2 @83 /
//     ALU0 @81 — I12 shares LUI with branches, so every arm pins opc=1).
//   R_HAYDN_CSR_UImm8_E3* — qualified twins (typed 27/23/54/85); I8 shares
//     CSRR/CSRW (opc 4/5) with ZERO_* (1..3), so the sniff pins {4,5}.
//   R_HAYDN_LO20 — ALU RI20 20-bit absolute field (specifier %lo20).
//     FieldLsb via resolveFieldLsb (E2 e0 ALU0 @31; E2 e1 ALU1 @65).
//     RI20 is E2-only — unrecognized parcels keep the table default.
//   R_HAYDN_PC_LO20 — same RI20 windows, PC-relative (specifier %pc_lo20).
//   R_HAYDN_32_PCREL — 32-bit data PC-rel (PIC/JT EK_LabelDifference32
//     `.long LBB - JT`; FK_Data_4+IsPCRel). Data-word R_PC, not a GOT/PLT ABI.
//   R_HAYDN_LS_IMM — Format E LS RI6 signed imm6 (not SImm16, not LO20).
//   R_HAYDN_GOT_HI20 — fail-closed: no PIC/GOT/PLT product ABI (never silent R_GOT / R_ABS).
//   R_HAYDN_TPREL_HI20/LO16 — fail-closed: golden has no TLS model, so these
//     kinds are a link error (never silent R_ABS).
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "InputSection.h"
#include "Symbols.h"
#include "Target.h"
#include "HaydnCallRelax.h"
#include "HaydnFormat.h"
#include "HaydnRelocLayout.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/bit.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"

#include <cstring>
#include <numeric>
#include <vector>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

// Object identity stays fail-closed: experimental EM_HAYDN=259 collides with
// the published Kalray KVX allocation; distinguisher is EF_HAYDN_E96=0x1.
// Symbolic JALR is ELF 22 only — do not remint the kind.
static_assert(ELF::EM_HAYDN == 259,
              "EM_HAYDN stays 259; do not invent a replacement (KVX collision)");
static_assert(ELF::EF_HAYDN_E96 == 0x1u,
              "EF_HAYDN_E96 stays 0x1; do not invent e_flags image-versioning");
static_assert(ELF::EF_HAYDN_E96 == llvm::haydn::format::EF_HAYDN_E96,
              "ELF and format-registry EF_HAYDN_E96 must stay identical");
static_assert(ELF::R_HAYDN_JALRSImm12 == 22,
              "do not remint R_HAYDN_JALRSImm12");
static_assert(static_cast<unsigned>(llvm::HaydnReloc::RelocKind::JALRSImm12) ==
                  ELF::R_HAYDN_JALRSImm12,
              "JALRSImm12 RelocKind must match ELF R_HAYDN_JALRSImm12");
static_assert(ELF::R_HAYDN_JALRSImm12 != ELF::R_HAYDN_WIDE_BranchSImm12_RI,
              "symbolic JALR must not alias the RI12 branch row");
static_assert(ELF::R_HAYDN_JALRSImm12 != ELF::R_HAYDN_32_PCREL,
              "symbolic JALR must not alias PIC/JT label-diff");

namespace {

/// Production parcel EncodedBytes (sole active profile: Format E / 12).
static llvm::haydn::format::EncodedBytes productParcelEncodedBytes() {
  auto B = llvm::haydn::format::maxEncodedBytesInProfile(
      llvm::haydn::format::ObjectEncodingProfileID::E96);
  // FE8: reject residual 8/16 dual product sizes at the LLD boundary.
  assert(B.Value == 12u && "LLD production EncodedBytes must be Format E 12");
  return B;
}

// Named HaydnELF (not Haydn) so out-of-line member defs do not collide with
// namespace llvm::Haydn pulled in via MCTargetDesc headers.
class HaydnELF final : public TargetInfo {
public:
  HaydnELF(Ctx &CtxRef) : TargetInfo(CtxRef) {
    // 32-bit, little-endian, baremetal
    pltRel = R_HAYDN_NONE;
    relativeRel = R_HAYDN_32;
    gotRel = R_HAYDN_32;
    symbolicRel = R_HAYDN_32;
    pltHeaderSize = 0;
    pltEntrySize = 0;
    defaultMaxPageSize = 4096;

    // Stay armed so out-of-range long branches/calls (Haydn::needsThunk)
    // reach addThunkHaydn and fail closed with the named D1.57 veneer-ABI
    // gap instead of a generic range failure; in-range sites link directly.
    // No Haydn thunk exists until ISA-70 approves a non-clobbering veneer
    // template, so island spacing keeps the 0 default below.
    needsThunks = true;

    // Executable padding: whole product parcels only (function/.text pack
    // is 0 mod EncodedBytes; a 4/8-byte trap fill would put later B/JAL
    // labels off the parcel grid). Consume the same generated full-slot
    // NOP idle as MC writeNopData (never all-zero).
    ArrayRef<uint8_t> Idle = llvm::haydn::format::canonicalFullSlotIdleParcel();
    assert(Idle.size() == productParcelEncodedBytes().Value &&
           "LLD idle parcel must match production EncodedBytes");
    assert(Idle.size() >= 4 &&
           "idle parcel must cover the 4-byte TargetInfo trapInstr field");
    // trapInstr is a fixed 4-byte generic LLD field (Writer::fillTrap /
    // OutputSection::getFiller); it lands in the sub-parcel residues that
    // whole-parcel nopInstrs cannot cover (function alignment sits on the
    // 4-byte lattice, so residues of 4 or 8 (mod EncodedBytes) are
    // unavoidable and can never hold a legal bundle). AIE tiles 1..8-byte
    // NOPs (AIE.cpp:27-35); Hexagon packet align is 4 (power of 2).
    // EncodedBytes=12 is a size/phase modulus, not sh_addralign, so those
    // peers cannot supply a 12-byte trap tile. Zero is the one ISA-inert
    // filler: indicator != 111 means the bytes are not code, and the
    // consumer coverage walk accepts zero gaps by inspecting the bytes.
    // Seeding this from the idle parcel put a 111-indicator prefix into
    // dead gaps and made the linear-sweep disassembler decode a phantom
    // bundle straddling the next function's entry (overlapping PCs ->
    // CODE_IMAGE_REJECT "direct control target is not an exact code record";
    // gcc-torture align-3). Whole-parcel gaps still use the generated
    // idle parcel via nopInstrs below.
    trapInstr = {0x00, 0x00, 0x00, 0x00};
    nopInstrs = std::vector<std::vector<uint8_t>>{
        std::vector<uint8_t>(Idle.begin(), Idle.end())};
  }

  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override {
    switch (type) {
    case R_HAYDN_32_PCREL:
    case R_HAYDN_PC_LO20:
    case R_HAYDN_BranchSImm16:
    case R_HAYDN_CallSImm20:
    case R_HAYDN_WIDE_BranchSImm12:
    case R_HAYDN_WIDE_BranchSImm12_RI:
    case R_HAYDN_WIDE_CallSImm20:
    case R_HAYDN_HWLoopOff1:
    case R_HAYDN_HWLoopOff2:
    case R_HAYDN_WIDE_CallSImm20_E3E1:
    case R_HAYDN_WIDE_BranchSImm12_E3E0:
    case R_HAYDN_WIDE_BranchSImm12_E3E1:
    case R_HAYDN_WIDE_BranchSImm12_E3E2:
    case R_HAYDN_WIDE_BranchSImm12_RI_E3E0:
    case R_HAYDN_WIDE_BranchSImm12_RI_E3E1:
      return R_PC;
    case R_HAYDN_JALRSImm12:
    case R_HAYDN_JALRSImm12_E3E0:
    case R_HAYDN_JALRSImm12_E3E1:
      // ISA-69: no golden relocation base. Do not return R_PC (that was
      // the unapproved S+A-P convention). ELF numbers stay; refuse.
      Err(ctx) << getErrorLoc(ctx, loc)
               << llvm::HaydnReloc::kUnsupportedSymbolicJalrDiag;
      return R_NONE;
    case R_HAYDN_PC_LO20_E1:
      // Qualified twin of R_HAYDN_PC_LO20.
      return R_PC;
    case R_HAYDN_NONE:
      return R_NONE;
    case R_HAYDN_32:
    case R_HAYDN_8:
    case R_HAYDN_16:
    case R_HAYDN_SImm16:
    case R_HAYDN_HI20:
    case R_HAYDN_LO16:
    case R_HAYDN_HI12:
    case R_HAYDN_LO20:
    case R_HAYDN_LS_IMM:
    case R_HAYDN_CSR_UImm8:
    case R_HAYDN_LO20_E1:
    case R_HAYDN_HI12_E3E0_ALU2:
    case R_HAYDN_HI12_E3E0_ALU0:
    case R_HAYDN_HI12_E3E1:
    case R_HAYDN_HI12_E3E2_ALU2:
    case R_HAYDN_HI12_E3E2_ALU0:
    case R_HAYDN_CSR_UImm8_E3E0_ALU2:
    case R_HAYDN_CSR_UImm8_E3E0_ALU0:
    case R_HAYDN_CSR_UImm8_E3E1:
    case R_HAYDN_CSR_UImm8_E3E2:
      // D1.17 qualified HI12/CSR twins: same R_ABS as the base kind; the
      // write window rides the typed table row (resolveFieldLsb
      // early-return via isEntryQualifiedKind), never a sniff.
      return R_ABS;
    case R_HAYDN_TPREL_HI20:
    case R_HAYDN_TPREL_LO16:
      // Golden has no TLS model. Mapping these to R_ABS silently linked
      // thread_local as absolute addresses. Fail closed.
      Err(ctx) << getErrorLoc(ctx, loc)
               << "Haydn TLS TPREL relocations are unsupported "
                  "(no golden TLS model); refusing silent R_ABS";
      return R_NONE;
    case R_HAYDN_GOT_HI20:
      // Baremetal static ABI: no PIC/GOT/PLT. Mapping to R_GOT would
      // allocate a GOT and rewrite as R_HAYDN_32 (gotRel). Fail closed.
      // JT/PIC label-diff stays R_HAYDN_32_PCREL (R_PC on a data word).
      Err(ctx) << getErrorLoc(ctx, loc)
               << "Haydn GOT relocations are unsupported "
                  "(no PIC/GOT/PLT product ABI); refusing silent R_GOT";
      return R_NONE;
    default:
      Err(ctx) << getErrorLoc(ctx, loc)
               << "unknown Haydn relocation type: " << type;
      return R_NONE;
    }
  }

  RelType getDynRel(RelType type) const override {
    if (type == R_HAYDN_32)
      return type;
    return R_HAYDN_NONE;
  }

  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override {
    if (type > R_HAYDN_CSR_UImm8_E3E2) {
      InternalErr(ctx, buf) << "cannot read addend for relocation " << type;
      return 0;
    }
    // RELA-unconsulted in the product path. The int64_t wrapper cannot
    // report diagnostics; the fail-closed site authority for the hwloop
    // windows lives in relocate (tryResolveFieldLsb, D1.42). A future
    // REL (implicit-addend) Haydn path needs its own error propagation
    // here — do not widen this seat ad hoc.
    return HaydnReloc::readRelocAddend(
        static_cast<HaydnReloc::RelocKind>(static_cast<unsigned>(type)), buf);
  }

  bool inBranchRange(RelType type, uint64_t src, uint64_t dst) const override {
    switch (type) {
    case R_HAYDN_BranchSImm16:
    case R_HAYDN_CallSImm20:
    case R_HAYDN_WIDE_CallSImm20:
    case R_HAYDN_WIDE_BranchSImm12:
    case R_HAYDN_WIDE_BranchSImm12_RI:
    case R_HAYDN_WIDE_CallSImm20_E3E1:
    case R_HAYDN_WIDE_BranchSImm12_E3E0:
    case R_HAYDN_WIDE_BranchSImm12_E3E1:
    case R_HAYDN_WIDE_BranchSImm12_E3E2:
    case R_HAYDN_WIDE_BranchSImm12_RI_E3E0:
    case R_HAYDN_WIDE_BranchSImm12_RI_E3E1:
    case R_HAYDN_HWLoopOff1:
    case R_HAYDN_HWLoopOff2: {
      HaydnReloc::RelocKind R =
          static_cast<HaydnReloc::RelocKind>(static_cast<unsigned>(type));
      int64_t Offset = static_cast<int64_t>(dst - src);
      return HaydnReloc::computeRelocValue(R, static_cast<uint64_t>(Offset)).OK;
    }
    case R_HAYDN_JALRSImm12:
    case R_HAYDN_JALRSImm12_E3E0:
    case R_HAYDN_JALRSImm12_E3E1:
      // ISA-69: no qualified relocation-base range oracle.
      return false;
    default:
      return true;
    }
  }

  void relocateAlloc(InputSection &sec, uint8_t *buf) const override;

  bool needsThunk(RelExpr expr, RelType type, const InputFile *file,
                  uint64_t branchAddr, const Symbol &s,
                  int64_t a) const override {
    switch (type) {
    case R_HAYDN_BranchSImm16:
    case R_HAYDN_CallSImm20:
    case R_HAYDN_WIDE_BranchSImm12:
    case R_HAYDN_WIDE_BranchSImm12_RI:
    case R_HAYDN_WIDE_CallSImm20:
    case R_HAYDN_WIDE_CallSImm20_E3E1:
    case R_HAYDN_WIDE_BranchSImm12_E3E0:
    case R_HAYDN_WIDE_BranchSImm12_E3E1:
    case R_HAYDN_WIDE_BranchSImm12_E3E2:
    case R_HAYDN_WIDE_BranchSImm12_RI_E3E0:
    case R_HAYDN_WIDE_BranchSImm12_RI_E3E1:
      return !inBranchRange(type, branchAddr, s.getVA(ctx, a));
    // JALRSImm12 is never veneered (ISA-69 fail-closed; not a PC-relative
    // long-branch).
    default:
      return false;
    }
  }

  // Gerrit reunification honors aligned(N) when N is a power of two.
  // EncodedBytes (12) is a size/phase modulus, not sh_addralign: LLD
  // assignOffsets / OutputSection placement call alignToPowerOf2, which
  // aborts on 0 and on non-2^n (Hexagon packet align is 4, Thunks.cpp:427;
  // AIE bundle align is 16). Floor a broken 0 / non-2^n exec align to the
  // function-alignment lattice (4) so ThunkSection and input placement
  // cannot inherit Align==0. Parcel-aligned SHF_EXECINSTR inputs set
  // nopFiller so EncodedBytes script/exec gaps use the idle NOP table
  // instead of tiling the 4-byte trap. Sub-parcel tails stay trap.
  //
  // -ffunction-sections + aligned(N>4) would otherwise start the next
  // input at alignToPowerOf2(prev_end, N), which is often 4 or 8 mod 12
  // (12-byte first function then 16-align lands at +16). BundleSim then
  // rejects JAL/B to that symbol ("direct control target is not an exact
  // code record"). Grow every exec input to lcm(maxAlign, EncodedBytes)
  // so the next 2^n start stays on the section's parcel phase. AIE does
  // not need this: emitCodeAlignment(Align(16)) is already 2^n
  // (AIETargetELFStreamer.cpp:73-81).
  void scanSection(InputSectionBase &sec) override {
    if (sec.flags & SHF_EXECINSTR) {
      const unsigned Parcel = productParcelEncodedBytes().Value;
      if (sec.addralign == 0 || !llvm::has_single_bit(sec.addralign))
        sec.addralign = 4;
      if (Parcel != 0 && sec.getSize() % Parcel == 0)
        sec.nopFiller = true;
    }
    TargetInfo::scanSection(sec);
    // Do not inflate InputSection::size to lcm(maxAlign, EncodedBytes).
    // Growing the file-backed size past content() makes Writer copy the
    // next bytes of the .o (symtab strings) into .text. BundleSim then
    // rejects the non-zero pad (align-3.c aligned(256) →
    // "uncovered bytes that are not alignment fill"). Output-section
    // p2align gaps use trapInstr (zeros), which accept_alignment_fill
    // accepts. Function addresses stay on the section's 4-mod-12 grid
    // because EncodedBytes is a multiple of Align(4).
  }

  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override {
    RelType type = rel.type;
    if (type == R_HAYDN_TPREL_HI20 || type == R_HAYDN_TPREL_LO16) {
      Err(ctx) << getErrorLoc(ctx, loc)
               << "Haydn TLS TPREL relocations are unsupported "
                  "(no golden TLS model); refusing silent R_ABS";
      return;
    }
    if (type == R_HAYDN_GOT_HI20) {
      Err(ctx) << getErrorLoc(ctx, loc)
               << "Haydn GOT relocations are unsupported "
                  "(no PIC/GOT/PLT product ABI); refusing silent R_GOT";
      return;
    }
    if (type == R_HAYDN_JALRSImm12 || type == R_HAYDN_JALRSImm12_E3E0 ||
        type == R_HAYDN_JALRSImm12_E3E1) {
      Err(ctx) << getErrorLoc(ctx, loc)
               << llvm::HaydnReloc::kUnsupportedSymbolicJalrDiag;
      return;
    }
    if (type > R_HAYDN_CSR_UImm8_E3E2) {
      Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << type;
      return;
    }
    HaydnReloc::RelocKind R =
        static_cast<HaydnReloc::RelocKind>(static_cast<unsigned>(type));
    HaydnReloc::RelocCompute Comp = HaydnReloc::computeRelocValue(R, val);
    if (!Comp.OK) {
      Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << type << ": "
               << Comp.Err;
      return;
    }
    const HaydnReloc::RelocFieldInfo &FI = HaydnReloc::getRelocFieldInfo(R);
    // WIDE_CallSImm20 / WIDE_BranchSImm12{,_RI} / HI12: E2 e0
    // table FieldLsb; E3 e0/e1/e2 (and E2 e1 LO20/PC_LO20) via
    // tryResolveFieldLsb. D1.42: the hwloop arm resolves windows ONLY from
    // the generated HwLoopSniffSites table; an unrecognized hwloop site is
    // a NAMED error (fail-closed), never a silent base-row patch — same
    // diagnose shape as the TPREL/GOT guards above.
    unsigned FieldLsb = 0;
    if (const char *SiteErr = HaydnReloc::tryResolveFieldLsb(R, loc, FieldLsb)) {
      Err(ctx) << getErrorLoc(ctx, loc) << SiteErr;
      return;
    }
    HaydnReloc::patchField(loc, Comp.FieldVal, FI.NBytes, FI.FieldSize,
                           FieldLsb);
  }

  uint32_t calcEFlags() const override {
    // Product output carries ELF::EF_HAYDN_E96 (provisional consumer
    // agreement; not an external e_machine allocation and not an
    // image-versioning scheme). Every participating object must already
    // stamp that flag — zero and unknown nonzero profiles reject
    // fail-closed (no silent upgrade). ctx.objectFiles includes extracted
    // archive members and startup objects after symbol resolution
    // (Driver.cpp calcEFlags seat). Peer: AIE.cpp:66-70 copies the first
    // object's flags; AIE ELF.h:498-502 publishes EF_AIE_*. Haydn overlay
    // requires exact ELF::EF_HAYDN_E96. Empty objectFiles (empty archive)
    // still stamps that flag — Hexagon empty-archive default analog
    // (hexagon-eflag.s), not a new e_machine.
    //
    // EM_HAYDN=259 is the experimental producer number (ELF.h). Some ELF
    // registries assign 259 to Kalray KVX. Do not invent a replacement
    // e_machine here. A 259 object without the production flag is rejected
    // so a KVX-like file cannot silently link as Haydn.
    const uint32_t Profile =
        llvm::haydn::format::getProductionObjectEncodingProfile().ELFFlagsValue;
    assert(Profile != 0 && "E96 product profile must allocate nonzero e_flags");
    if (Profile != ELF::EF_HAYDN_E96) {
      ErrAlways(ctx) << "Haydn production ELFFlagsValue 0x"
                     << Twine::utohexstr(Profile)
                     << " is not EF_HAYDN_E96; refusing invented e_flags";
    }
    const uint32_t Expected = ELF::EF_HAYDN_E96;
    for (InputFile *f : ctx.objectFiles) {
      const auto &Hdr = cast<ObjFile<ELF32LE>>(f)->getObj().getHeader();
      if (Hdr.e_machine != EM_HAYDN) {
        ErrAlways(ctx) << f << ": unexpected e_machine "
                       << unsigned(Hdr.e_machine)
                       << "; Haydn objects must be EM_HAYDN";
        continue;
      }
      uint32_t eflags = Hdr.e_flags;
      if (eflags != Expected) {
        ErrAlways(ctx) << f << ": incompatible e_flags 0x"
                       << Twine::utohexstr(eflags)
                       << "; expected Format E ABI flag 0x"
                       << Twine::utohexstr(Expected)
                       << " (EM_HAYDN=259 experimental; refuse objects that "
                          "reuse 259 without this flag)";
        continue;
      }
    }
    return Expected;
  }

  // D1.57: no Haydn veneer exists (ISA-70 ABI not approved), so
  // getThunkSectionSpacing keeps the 0 default — no pre-created thunk
  // islands. Restoring islands belongs with the ISA-70 template, not here.

};

void HaydnELF::relocateAlloc(InputSection &sec, uint8_t *buf) const {
  TargetInfo::relocateAlloc(sec, buf);
  if (!(sec.flags & SHF_EXECINSTR))
    return;
  const size_t Have = sec.content().size();
  const size_t Want = sec.getSize();
  if (Want > Have) {
    // Zero the tail. Idle-parcel fill here used to leave 111-indicator
    // bytes that were not in the objdump record stream, so BundleSim
    // treated them as uncovered non-fill (align-3.c). Zeros cannot be a
    // Format-E record (indicator != 111) and are the documented
    // accept_alignment_fill alphabet.
    memset(buf + Have, 0, Want - Have);
  }

  // Cycle-neutral returning-call relax (RISC-V relaxCall peer; Haydn overlay).
  // ISel emits LUI+ADDI32+JALR. When |target-pc| fits CallSImm20 AND the
  // JALR is a matching returning call (rd=LR, rs=LUI.rd=ADDI.rd), rewrite
  // only the JALR parcel to JAL. Leave LUI+ADDI in place so the address
  // temp stays live for later jalr lr, temp. Long jumps (rd != LR) and
  // coincidental JALR after a different LUI+ADDI stay JALR. Do not change
  // section size or packet count. --no-relax keeps the general form.
  if (!ctx.arg.relax)
    return;
  const unsigned Parcel = productParcelEncodedBytes().Value;
  if (Parcel == 0 || Have < 3u * Parcel)
    return;
  ArrayRef<uint8_t> Idle = llvm::haydn::format::canonicalFullSlotIdleParcel();
  if (Idle.size() != Parcel)
    return;

  for (const Relocation &Hi : sec.relocs()) {
    if (Hi.type != R_HAYDN_HI12 || !Hi.sym)
      continue;
    const uint64_t LoOff = Hi.offset + Parcel;
    const uint64_t JalOff = Hi.offset + 2u * Parcel;
    if (JalOff + Parcel > Have)
      continue;
    const Relocation *Lo = nullptr;
    for (const Relocation &R : sec.relocs()) {
      if (R.type == R_HAYDN_LO20 && R.offset == LoOff && R.sym == Hi.sym &&
          R.addend == Hi.addend) {
        Lo = &R;
        break;
      }
    }
    if (!Lo)
      continue;
    uint8_t *LuiP = buf + Hi.offset;
    uint8_t *AddiP = buf + LoOff;
    uint8_t *JalrP = buf + JalOff;
    if (!llvm::haydn::call_relax::isReturningCallRelaxTriple(LuiP, AddiP, JalrP,
                                                            Parcel))
      continue;
    unsigned RtEnc = 0;
    if (!llvm::haydn::call_relax::readE2E0DestEnc(JalrP, Parcel, RtEnc))
      continue;
    const uint64_t JalPC = sec.getVA(JalOff);
    const uint64_t Dest = Hi.sym->getVA(ctx, Hi.addend);
    if (!inBranchRange(R_HAYDN_WIDE_CallSImm20, JalPC, Dest))
      continue;
    uint8_t Jal[32];
    if (Parcel > sizeof(Jal))
      continue;
    if (!llvm::haydn::call_relax::writeE2JalSingleton(Jal, Parcel, RtEnc))
      continue;
    const int64_t Disp = static_cast<int64_t>(Dest - JalPC);
    HaydnReloc::RelocCompute Comp = HaydnReloc::computeRelocValue(
        HaydnReloc::RelocKind::WIDE_CallSImm20,
        static_cast<uint64_t>(Disp));
    if (!Comp.OK)
      continue;
    unsigned FieldLsb = 0;
    if (const char *SiteErr = HaydnReloc::tryResolveFieldLsb(
            HaydnReloc::RelocKind::WIDE_CallSImm20, Jal, FieldLsb)) {
      (void)SiteErr;
      continue;
    }
    const HaydnReloc::RelocFieldInfo &FI =
        HaydnReloc::getRelocFieldInfo(HaydnReloc::RelocKind::WIDE_CallSImm20);
    HaydnReloc::patchField(Jal, Comp.FieldVal, FI.NBytes, FI.FieldSize,
                           FieldLsb);
    memcpy(JalrP, Jal, Parcel);
  }
}

} // namespace

void elf::setHaydnTargetInfo(Ctx &ctx) {
  ctx.target.reset(new HaydnELF(ctx));
}
