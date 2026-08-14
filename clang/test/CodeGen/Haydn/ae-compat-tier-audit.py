#!/usr/bin/env python3
"""Fail-closed AE_* tier inventory audit.

Compares public #define AE_* macros in haydn_dsp.h against HaydnAeCompat
records in BuiltinsHaydn.td. Scans every product tier for width-mismatch
(maxabs32-under-16, scalar dual-24, bag-OR SEL*, dual ASR high-lane drop),
NEG_PC probe-only seed parity, SA64 store-finish dir ImmArg, and residual
F24/SELP24/sat-shift class pins.

Usage:
  ae-compat-tier-audit.py <haydn_dsp.h> <BuiltinsHaydn.td>
"""
from __future__ import annotations

import re
import sys
from pathlib import Path


def parse_public_ae_macros(text: str) -> dict[str, str]:
    lines = text.splitlines()
    macros: dict[str, str] = {}
    i = 0
    while i < len(lines):
        m = re.match(
            r"\s*#\s*define\s+(AE_[A-Za-z0-9_]+)(\([^\)]*\))?\s*(.*)$", lines[i]
        )
        if m and not m.group(1).startswith("__"):
            name = m.group(1)
            body = m.group(3)
            while body.rstrip().endswith("\\"):
                i += 1
                if i >= len(lines):
                    break
                body = body.rstrip()[:-1] + " " + lines[i].strip()
            macros[name] = body
        i += 1
    return macros


def parse_public_ae_inlines(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for m in re.finditer(r"\b(AE_[A-Za-z0-9_]+)\s*\([^)]*\)\s*\{", text):
        name = m.group(1)
        if name not in out:
            out[name] = text[m.start() : m.start() + 900]
    return out


def parse_td_tiers(text: str) -> dict[str, tuple[str, str]]:
    """Parse HaydnAeCompatDef records (single-line ';' or multi-line '{')."""
    out: dict[str, tuple[str, str]] = {}
    # AE-P0 and residual class use multi-line bodies (OracleId/DirImm/MemEffect).
    # A trailing ';' only matches the old single-line form and silently drops
    # those records — untagged surface then fails closed for the wrong reason.
    pat = re.compile(
        r'def\s+(AE_[A-Za-z0-9_]+)\s*:\s*HaydnAeCompatDef\s*<\s*"([^"]+)"\s*,\s*"([^"]*)"\s*>\s*[;{]'
    )
    for m in pat.finditer(text):
        out[m.group(1)] = (m.group(2), m.group(3))
    return out


def parse_td_oracle_fields(text: str) -> dict[str, dict[str, str]]:
    """Parse DeclKind/OracleId/DirImm/SoftState/MemEffect from multi-line bodies."""
    out: dict[str, dict[str, str]] = {}
    # Match def ... HaydnAeCompatDef<...> { ... } or bare ;
    rec = re.compile(
        r'def\s+(AE_[A-Za-z0-9_]+)\s*:\s*HaydnAeCompatDef\s*<\s*"([^"]+)"\s*,\s*"([^"]*)"\s*>\s*(?:;|\{([^}]*)\})',
        re.M,
    )
    for m in rec.finditer(text):
        name = m.group(1)
        body = m.group(4) or ""
        fields: dict[str, str] = {
            "tier": m.group(2),
            "lowering": m.group(3),
            "DeclKind": "",
            "OracleId": "",
            "DirImm": "-1",
            "SoftState": "0",
            "MemEffect": "0",
        }
        for key in ("DeclKind", "OracleId"):
            km = re.search(r'let\s+' + key + r'\s*=\s*"([^"]*)"\s*;', body)
            if km:
                fields[key] = km.group(1)
        for key in ("DirImm", "SoftState", "MemEffect"):
            km = re.search(r'let\s+' + key + r'\s*=\s*(-?\d+)\s*;', body)
            if km:
                fields[key] = km.group(1)
        out[name] = fields
    return out


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: ae-compat-tier-audit.py haydn_dsp.h BuiltinsHaydn.td", file=sys.stderr)
        return 2
    dsp = Path(argv[1]).read_text(encoding="utf-8", errors="replace")
    td = Path(argv[2]).read_text(encoding="utf-8", errors="replace")

    macros = parse_public_ae_macros(dsp)
    inlines = parse_public_ae_inlines(dsp)
    surface: dict[str, str] = dict(inlines)
    surface.update(macros)
    tiers = parse_td_tiers(td)

    errors: list[str] = []

    if not macros:
        errors.append("no public AE_* macros found in haydn_dsp.h")
    if not tiers:
        errors.append("no HaydnAeCompatDef records found in BuiltinsHaydn.td")

    untagged = sorted(set(surface) - set(tiers))
    orphan_td = sorted(set(tiers) - set(surface))
    if untagged:
        errors.append(
            f"{len(untagged)} public AE_* surface entries untagged in TD (sample): "
            + ", ".join(untagged[:12])
        )
    if orphan_td:
        errors.append(
            f"{len(orphan_td)} TD HaydnAeCompat symbols missing from haydn_dsp.h surface (sample): "
            + ", ".join(orphan_td[:12])
        )

    # Closed-surface floor: opt-in residual was ~64; full public AE_* is >= 600.
    # Shrink reopens the SRAS32-class untagged-macro hole.
    INVENTORY_FLOOR = 600
    if len(macros) < INVENTORY_FLOOR:
        errors.append(
            f"public AE_* macro inventory {len(macros)} < floor {INVENTORY_FLOOR}"
        )
    if len(tiers) < INVENTORY_FLOOR:
        errors.append(
            f"TD HaydnAeCompat inventory {len(tiers)} < floor {INVENTORY_FLOOR}"
        )
    if len(surface) != len(tiers) and not untagged and not orphan_td:
        # Defensive: equal sets already imply equal counts; keep explicit pin.
        errors.append(
            f"surface/td count mismatch surface={len(surface)} tiers={len(tiers)}"
        )

    def require(sym: str, tier: str) -> None:
        if sym not in tiers:
            errors.append(f"missing required TD tier for {sym}")
            return
        if tiers[sym][0] != tier:
            errors.append(f"{sym} tier is {tiers[sym][0]!r}, want {tier!r}")

    require("AE_MAXABS16S", "EMULATED")
    require("AE_ADD64X2_", "UNSUPPORTED")
    require("AE_ADD64X2_vector", "UNSUPPORTED")
    require("AE_LA16X4NEG_PC", "EXACT")
    require("AE_LA32X2NEG_PC", "EXACT")
    require("AE_LA16X4POS_PC", "EXACT")
    require("AE_LA32X2POS_PC", "EXACT")
    # Dual-24 F24 POS seed peers (PLDWWUA; probe-only, no reverse invent).
    require("AE_LA32X2F24POS_PC", "EXACT")
    require("AE_LA24X2POS_PC", "EXACT")
    # Store-finish residual: POS dir0 / NEG dir1 (not seed-class POS alias).
    require("AE_SA64POS_FP", "EMULATED")
    require("AE_SA64NEG_FP", "EMULATED")
    # Residual dual-24 / lane-select / sat-shift class (fail-closed inventory).
    for sym in (
        "AE_SELP24_HH",
        "AE_SELP24_HL",
        "AE_SELP24_LH",
        "AE_SELP24_LL",
        "AE_SEL24_HH",
        "AE_SEL24_HL",
        "AE_SEL24_LH",
        "AE_SEL24_LL",
        "AE_SEL32_HH",
        "AE_SEL32_HL",
        "AE_SEL32_LH",
        "AE_SEL32_LL",
        "AE_NEG24S",
        "AE_NEGSP24S",
        "AE_F24X2_SRAI",
        "AE_F32X2_SRAI",
        "AE_SRAI24",
        "AE_SRAIP24",
        "AE_ADDP24",
        "AE_ADD24S",
        "AE_SUB24S",
        "AE_ADDSP24S",
        "AE_SUBSP24S",
        "AE_ZERO24",
        "AE_L32X2_RIC",
        "AE_L32X2F24_RIC",
        "AE_L16X4_RIC",
        "AE_LA16X4_RIC",
        "AE_LA32X2_RIC",
        "AE_LA32X2F24_RIC",
        # Dual-24 unaligned circular residual (AR + CBR forward IC).
        "AE_LA32X2F24_IC",
        "AE_SA32X2F24_IC",
        "AE_LA24X2_IC",
        "AE_SA24X2_IC",
        # Base unaligned circular residual (non-F24 AR+CBR peers of dual-24 IC).
        "AE_LA16X4_IC",
        "AE_LA32X2_IC",
        "AE_SA16X4_IC",
        "AE_SA32X2_IC",
        "AE_LA32X2F24_XC",
        "AE_SA32X2F24_XC",
        # Reverse unaligned post-inc residual (UA dir=1).
        "AE_LA16X4_RIP",
        "AE_LA32X2_RIP",
        "AE_LA32X2F24_RIP",
        "AE_SA16X4_RIP",
        "AE_SA32X2_RIP",
        "AE_SA32X2F24_RIP",
        # Aligned reverse linear RIP residual.
        "AE_L16X4_RIP",
        "AE_L32X2_RIP",
        "AE_L32X2F24_RIP",
        "AE_S32X2_RIP",
        "AE_S32X2F24_RIP",
        # Dual-24 unaligned forward IP residual.
        "AE_LA32X2F24_IP",
        "AE_SA32X2F24_IP",
        "AE_LA24X2_IP",
        "AE_SA24X2_IP",
        # Base aligned XC residual.
        "AE_L32X2_XC",
        "AE_S32X2_XC",
        "AE_L16X4_XC",
        "AE_S16X4_XC",
        # Dual-24 aligned F24 XC residual (same D_LDW/SDW_CB as base XC).
        "AE_L32X2F24_XC",
        "AE_S32X2F24_XC",
        "AE_SRAS32",
        "AE_SLAS32",
    ):
        require(sym, "EXACT")
    require("AE_SLAI24S", "EMULATED")
    require("AE_SLAI64S", "EMULATED")
    require("AE_SLAS32S", "EMULATED")
    require("AE_SLAA64S", "EMULATED")
    require("AE_SLAS64S", "EMULATED")
    # Hot IIR soft sat-left residual (F64/F32x2); never plain << wrap.
    require("AE_F64_SLAIS", "EMULATED")
    require("AE_F32X2_SLAIS", "EMULATED")
    require("AE_F64_SLAS", "EMULATED")
    for pos in ("AE_LA16X4POS_PC", "AE_LA32X2POS_PC", "AE_LA32X2F24POS_PC"):
        if pos in tiers and tiers[pos][1] != "haydn_ae_la64_pp":
            errors.append(
                f"{pos} lowering is {tiers[pos][1]!r}, want haydn_ae_la64_pp"
            )
    if "AE_LA24X2POS_PC" in tiers:
        low24 = tiers["AE_LA24X2POS_PC"][1]
        if low24 not in ("header", "haydn_ae_la64_pp"):
            errors.append(
                f"AE_LA24X2POS_PC lowering is {low24!r}, want header or "
                "haydn_ae_la64_pp"
            )
        body24 = macros.get("AE_LA24X2POS_PC", "")
        if body24 and "AE_LA32X2F24POS_PC" not in body24 and "haydn_ae_la64_pp" not in body24:
            errors.append(
                "AE_LA24X2POS_PC body must alias AE_LA32X2F24POS_PC / la64_pp"
            )

    def require_token(sym: str, token: str) -> None:
        if sym not in tiers:
            return
        low = tiers[sym][1].lower()
        if low in ("", "header"):
            return
        if token not in low:
            errors.append(
                f"{sym} lowering {tiers[sym][1]!r} must name {token!r}"
            )

    # Reverse-circular residual: TD lowering must name reverse path tokens.
    require_token("AE_L32X2_RIC", "ldw_cb_imm")
    require_token("AE_L16X4_RIC", "ldw_cb_imm")
    require_token("AE_L32X2F24_RIC", "ldw_cb_imm")
    require_token("AE_LA16X4_RIC", "la16x4_step")
    require_token("AE_LA16X4_RIC", "cbr_step")
    require_token("AE_LA32X2_RIC", "la64_step")
    require_token("AE_LA32X2_RIC", "cbr_step")
    require_token("AE_LA32X2F24_RIC", "la64_step")
    require_token("AE_LA32X2F24_RIC", "cbr_step")
    # Dual-24 unaligned circular residual: forward IC AR + CBR tokens.
    require_token("AE_LA32X2F24_IC", "la64_step")
    require_token("AE_LA32X2F24_IC", "cbr_step")
    require_token("AE_SA32X2F24_IC", "sa64_step")
    require_token("AE_SA32X2F24_IC", "cbr_step")
    require_token("AE_LA24X2_IC", "la64_step")
    require_token("AE_LA24X2_IC", "cbr_step")
    require_token("AE_SA24X2_IC", "sa64_step")
    require_token("AE_SA24X2_IC", "cbr_step")
    # Base unaligned circular residual peers of dual-24 IC.
    require_token("AE_LA16X4_IC", "la16x4_step")
    require_token("AE_LA16X4_IC", "cbr_step")
    require_token("AE_LA32X2_IC", "la64_step")
    require_token("AE_LA32X2_IC", "cbr_step")
    require_token("AE_SA16X4_IC", "sa16x4_step")
    require_token("AE_SA16X4_IC", "cbr_step")
    require_token("AE_SA32X2_IC", "sa64_step")
    require_token("AE_SA32X2_IC", "cbr_step")
    require_token("AE_LA32X2F24_XC", "la64_step")
    require_token("AE_LA32X2F24_XC", "cbr_step")
    require_token("AE_SA32X2F24_XC", "sa64_step")
    require_token("AE_SA32X2F24_XC", "cbr_step")
    # Reverse unaligned post-inc residual: UA reverse step tokens.
    require_token("AE_LA16X4_RIP", "la16x4_step")
    require_token("AE_LA32X2_RIP", "la64_step")
    require_token("AE_LA32X2F24_RIP", "la64_step")
    require_token("AE_SA16X4_RIP", "sa16x4_step")
    require_token("AE_SA32X2_RIP", "sa64_step")
    require_token("AE_SA32X2F24_RIP", "sa64_step")
    # Dual-24 unaligned forward IP residual.
    require_token("AE_LA32X2F24_IP", "la64_step")
    require_token("AE_SA32X2F24_IP", "sa64_step")
    require_token("AE_LA24X2_IP", "la64_step")
    require_token("AE_SA24X2_IP", "sa64_step")
    # Base / dual-24 aligned XC residual: CB path tokens.
    require_token("AE_L32X2_XC", "ldw_cb_imm")
    require_token("AE_S32X2_XC", "sdw_cb_imm")
    require_token("AE_L16X4_XC", "ldw_cb_imm")
    require_token("AE_S16X4_XC", "sdw_cb_imm")
    require_token("AE_L32X2F24_XC", "ldw_cb_imm")
    require_token("AE_S32X2F24_XC", "sdw_cb_imm")

    require_token("AE_SELP24_HH", "x2sel32_hh")
    require_token("AE_SELP24_HL", "x2sel32_hl")
    require_token("AE_SELP24_LH", "x2sel32_lh")
    require_token("AE_SELP24_LL", "x2sel32_ll")
    require_token("AE_SEL24_HH", "x2sel32_hh")
    require_token("AE_SEL24_HL", "x2sel32_hl")
    require_token("AE_SEL24_LH", "x2sel32_lh")
    require_token("AE_SEL24_LL", "x2sel32_ll")
    require_token("AE_SEL32_HH", "x2sel32_hh")
    require_token("AE_SEL32_HL", "x2sel32_hl")
    require_token("AE_SEL32_LH", "x2sel32_lh")
    require_token("AE_SEL32_LL", "x2sel32_ll")
    require_token("AE_NEG24S", "x2neg32s")
    require_token("AE_F24X2_SRAI", "x2sra32")
    require_token("AE_F32X2_SRAI", "x2sra32")
    require_token("AE_ADDP24", "x2add32")
    require_token("AE_ADD24S", "x2add32s")
    require_token("AE_SUB24S", "x2sub32s")
    # SRAS/SLAS residual hygiene: dual SAR shift, not scalar ashr/shl.
    require_token("AE_SRAS32", "x2sra32")
    require_token("AE_SLAS32", "x2sll32")
    # Soft sat-left residual: must name sat helpers, not plain non-sat shift.
    require_token("AE_SLAI24S", "slaa32s")
    require_token("AE_SLAS32S", "slaa32s")
    require_token("AE_SLAI64S", "slaa64s")
    require_token("AE_SLAA64S", "slaa64s")
    require_token("AE_SLAS64S", "slaa64s")
    require_token("AE_F64_SLAIS", "slaa64s")
    require_token("AE_F32X2_SLAIS", "slaa32s")
    require_token("AE_F64_SLAS", "slaa64s")
    # Store-finish residual dir ImmArg pins (TD lowering tokens).
    require_token("AE_SA64POS_FP", "sa64pos")
    require_token("AE_SA64POS_FP", "dir0")
    require_token("AE_SA64NEG_FP", "sa64pos")
    require_token("AE_SA64NEG_FP", "dir1")

    # haydn_dsp.h must not re-stamp tier tags (single source is HaydnAeCompat).
    if re.search(r"#\s*define\s+HAYDN_COMPAT_TIER_AE_", dsp):
        errors.append(
            "haydn_dsp.h must not #define HAYDN_COMPAT_TIER_AE_* "
            "(tags come from generated haydn.h only)"
        )

    n_exact = sum(1 for t, _ in tiers.values() if t == "EXACT")
    n_emu = sum(1 for t, _ in tiers.values() if t == "EMULATED")
    n_unsup = sum(1 for t, _ in tiers.values() if t == "UNSUPPORTED")
    # Floor tracks curated residual class growth (dual-24 IC + base LA/SA IC +
    # reverse unaligned RIP + dual-24 IP + SEL32 + F32 ASR + dual-24 aligned
    # F24 XC). Shrink reopens plain-mem IC/IP/XC holes under dual names.
    if n_exact < 70:
        errors.append(f"EXACT tier count {n_exact} < floor 70 (curated peers)")
    if n_emu < 500:
        errors.append(f"EMULATED tier count {n_emu} < floor 500")
    # Permanent UNSUPPORTED set is closed: dual-64 adds only. Floor alone
    # would allow silent growth of the quarantine without product review.
    permanent_unsup = ["AE_ADD64X2_", "AE_ADD64X2_vector"]
    unsup_names = sorted(n for n, (t, _) in tiers.items() if t == "UNSUPPORTED")
    if unsup_names != permanent_unsup:
        errors.append(
            "TD UNSUPPORTED set must be exactly "
            f"{permanent_unsup} (got {unsup_names})"
        )
    if n_unsup != len(permanent_unsup):
        errors.append(
            f"UNSUPPORTED tier count {n_unsup} != {len(permanent_unsup)} "
            "(closed dual-64 quarantine only)"
        )

    if "AE_MAXABS16S" in tiers:
        maxabs_low = tiers["AE_MAXABS16S"][1].lower()
        if "maxabs32" in maxabs_low:
            errors.append("AE_MAXABS16S lowering must not mention maxabs32*")
        if "x4abs16s" not in maxabs_low or "x4max16" not in maxabs_low:
            errors.append(
                "AE_MAXABS16S lowering must name x4abs16s+x4max16 composite"
            )
    # Helpers are __AE_MAXABS16S_{1,2}; scan a local window of haydn_dsp.h.
    if "AE_MAXABS16S" in macros:
        mpos = dsp.find("#define AE_MAXABS16S")
        if mpos < 0:
            mpos = dsp.find("AE_MAXABS16S")
        window = dsp[mpos : mpos + 600].lower() if mpos >= 0 else ""
        if "maxabs32" in window:
            errors.append("AE_MAXABS16S macro/helpers must not call maxabs32*")
        if "x4abs16s" not in window:
            errors.append("AE_MAXABS16S helpers must call haydn_x4abs16s")
        if "x4max16" not in window:
            errors.append("AE_MAXABS16S 2-arg helper must call haydn_x4max16")

    # NEG_PC probe-only: same seed lowering as POS (haydn_ae_la64_pp); no invent.
    for neg, pos in (
        ("AE_LA16X4NEG_PC", "AE_LA16X4POS_PC"),
        ("AE_LA32X2NEG_PC", "AE_LA32X2POS_PC"),
    ):
        if neg in tiers:
            if tiers[neg][1] != "haydn_ae_la64_pp":
                errors.append(
                    f"{neg} lowering is {tiers[neg][1]!r}, want haydn_ae_la64_pp"
                )
            if pos in tiers and tiers[neg][1] != tiers[pos][1]:
                errors.append(
                    f"{neg} lowering {tiers[neg][1]!r} != {pos} {tiers[pos][1]!r}"
                )
            low = tiers[neg][1].lower()
            if any(tok in low for tok in ("predec", "reverse", "ric", "rip")):
                errors.append(f"{neg} invents reverse-direction seed: {tiers[neg][1]!r}")
        body = macros.get(neg, "")
        if body and "POS_PC" not in body and "haydn_ae_la64_pp" not in body:
            errors.append(
                f"{neg} macro body must alias POS_PC / haydn_ae_la64_pp (probe-only)"
            )
        # Body must not invent reverse seed helpers (direction lives on RIC/RIP).
        if body and any(
            tok in body.lower()
            for tok in ("predec", "la64_step", "la16x4_step", "cbr_step")
        ):
            errors.append(
                f"{neg} body invents reverse-direction seed (must alias POS_PC only)"
            )
    # Dual-24 F24 POS seed must match base 32x2 POS (PLDWWUA parity).
    if "AE_LA32X2F24POS_PC" in tiers and "AE_LA32X2POS_PC" in tiers:
        if tiers["AE_LA32X2F24POS_PC"][1] != tiers["AE_LA32X2POS_PC"][1]:
            errors.append(
                "AE_LA32X2F24POS_PC lowering must match AE_LA32X2POS_PC "
                f"({tiers['AE_LA32X2F24POS_PC'][1]!r} vs "
                f"{tiers['AE_LA32X2POS_PC'][1]!r})"
            )
    f24pos_body = macros.get("AE_LA32X2F24POS_PC", "")
    if f24pos_body and "haydn_ae_la64_pp" not in f24pos_body:
        errors.append(
            "AE_LA32X2F24POS_PC body must call haydn_ae_la64_pp (PLDWWUA seed)"
        )
    if f24pos_body and any(
        tok in f24pos_body.lower()
        for tok in ("predec", "la64_step", "la16x4_step", "cbr_step")
    ):
        errors.append(
            "AE_LA32X2F24POS_PC invents reverse-direction seed (must be PLDWWUA only)"
        )

    # Reverse-circular residual: late helpers must use reverse stride/dir,
    # never silent-alias forward IC (dir=0 / +8 wrap).
    def helper_window(name: str, span: int = 480) -> str:
        pos = dsp.rfind(f"#define {name}")
        if pos < 0:
            return ""
        return dsp[pos : pos + span]

    for helper, must_have in (
        ("__AE_L32X2_RIC_4A", ("haydn_ldw_cb_imm", "-((offs)")),
        ("__AE_L16X4_RIC_4A", ("haydn_ldw_cb_imm", "-((offs)")),
        ("__AE_L32X2F24_RIC_4A", ("haydn_ldw_cb_imm", "-((offs)")),
        (
            "__AE_LA16X4_RIC_4A",
            ("haydn_ae_la16x4_step", ", 1)", "haydn_cbr_step", "-8"),
        ),
        (
            "__AE_LA32X2_RIC_4A",
            ("haydn_ae_la64_step", ", 1)", "haydn_cbr_step", "-8"),
        ),
    ):
        win = helper_window(helper)
        if not win:
            errors.append(f"missing reverse residual helper {helper}")
            continue
        for tok in must_have:
            if tok not in win:
                errors.append(f"{helper} must contain reverse token {tok!r}")
        if helper.startswith("__AE_LA") and "RIC" in helper:
            if re.search(r"haydn_ae_la(?:16x4|64)_step\([^;]*,\s*0\s*\)", win):
                errors.append(f"{helper} silent-aliases forward IC (dir=0)")

    # AE_LA32X2F24_RIC is a direct macro (no __AE_ helper); pin reverse body.
    f24_ric = macros.get("AE_LA32X2F24_RIC", "")
    # parse_public_ae_macros may only keep first line of multi-line body;
    # fall back to a source window when the single-line body is incomplete.
    if "haydn_ae_la64_step" not in f24_ric or "haydn_cbr_step" not in f24_ric:
        f24_ric = helper_window("AE_LA32X2F24_RIC", 360)
    if not f24_ric:
        errors.append("missing AE_LA32X2F24_RIC reverse residual body")
    else:
        for tok in ("haydn_ae_la64_step", ", 1)", "haydn_cbr_step", "-8"):
            if tok not in f24_ric:
                errors.append(
                    f"AE_LA32X2F24_RIC body must contain reverse token {tok!r}"
                )
        if re.search(r"haydn_ae_la64_step\([^;]*,\s*0\s*\)", f24_ric):
            errors.append("AE_LA32X2F24_RIC silent-aliases forward IC (dir=0)")

    # Dual-24 unaligned circular residual: late helpers must use AR step +
    # soft CBR wrap (forward dir=0). Never plain mem or aligned D_LDW_CB only.
    for helper, must_have in (
        (
            "__AE_LA32X2F24_IC_4A",
            ("haydn_ae_la64_step", ", 0)", "haydn_cbr_step"),
        ),
        (
            "__AE_SA32X2F24_IC_4A",
            ("haydn_ae_sa64_step", ", 0)", "haydn_cbr_step"),
        ),
        # Base unaligned circular residual peers (non-F24).
        (
            "__AE_LA16X4_IC_4A",
            ("haydn_ae_la16x4_step", ", 0)", "haydn_cbr_step"),
        ),
        (
            "__AE_LA32X2_IC_4A",
            ("haydn_ae_la64_step", ", 0)", "haydn_cbr_step"),
        ),
        (
            "__AE_SA16X4_IC_4A",
            ("haydn_ae_sa16x4_step", ", 0)", "haydn_cbr_step"),
        ),
        (
            "__AE_SA32X2_IC_4A",
            ("haydn_ae_sa64_step", ", 0)", "haydn_cbr_step"),
        ),
    ):
        win = helper_window(helper, 420)
        if not win:
            errors.append(f"missing dual-24 IC residual helper {helper}")
            continue
        for tok in must_have:
            if tok not in win:
                errors.append(f"{helper} must contain dual-24 IC token {tok!r}")
        if "haydn_ldw_cb_imm" in win and "haydn_ae_la64_step" not in win:
            errors.append(f"{helper} must not silent-alias aligned CB only")
    # Public dual-24 IC aliases (late overload helpers) must resolve to the
    # F24 IC helpers — not early plain-mem / aligned-CB placeholders.
    for helper, peer in (
        ("__AE_LA24X2_IC_4A", "__AE_LA32X2F24_IC_4A"),
        ("__AE_SA24X2_IC_4A", "__AE_SA32X2F24_IC_4A"),
    ):
        win = helper_window(helper, 200)
        if not win:
            errors.append(f"missing dual-24 IC alias helper {helper}")
        elif peer not in win:
            errors.append(f"{helper} must route via {peer}")

    # Width-mismatch / silent-scalar residual: apply to every tier, not only
    # EXACT. SRAS32-class holes were untagged *or* mis-tagged EMULATED with a
    # width-divergent body (MAXABS16S via maxabs32s). UNSUPPORTED must not
    # claim a false dual-lane map either.
    for sym, (tier, lowering) in tiers.items():
        body = macros.get(sym, "")
        blob = (lowering + " " + body).lower()
        if "16" in sym and "maxabs32" in blob:
            errors.append(
                f"{tier} {sym} width-mismatch: maxabs32 under 16-lane name"
            )
        if "MAXABS16" in sym and "maxabs32" in blob:
            errors.append(f"{tier} {sym} must not lower via maxabs32*")
        # Dual-24 / SEL* residual: no scalar high-lane drop, no bag OR.
        # Applies to every product tier (all-tier width ban). SEL32 is the
        # dual-32 peer of SELP24; bag bitwise OR under SEL* is silent-wrong.
        dual_lane = any(t in sym for t in ("24", "SELP24", "SEL24", "SEL32"))
        if dual_lane and tier in ("EXACT", "EMULATED", "UNSUPPORTED"):
            if tier != "UNSUPPORTED":
                if "neg32s" in blob and "x2neg32s" not in blob:
                    errors.append(
                        f"{tier} {sym} width-mismatch: scalar neg32s under dual-24"
                    )
                if (
                    ("add32s" in blob or "add32" in blob)
                    and "x2add" not in blob
                    and "x2sub" not in blob
                    and "slaa32" not in blob
                    and "slas32" not in blob
                    and "x2sel" not in blob
                    and "header" not in blob
                ):
                    # Tolerate pure header aliases (ADDSP24S → ADD24S).
                    if "ae_add" not in body.lower() and "ae_sub" not in body.lower():
                        errors.append(
                            f"{tier} {sym} width-mismatch: scalar add under dual-24"
                        )
            is_sel = (
                "SELP24" in sym
                or re.fullmatch(r"AE_SEL24_..", sym)
                or re.fullmatch(r"AE_SEL32_..", sym)
            )
            if is_sel and tier != "UNSUPPORTED":
                if "x2sel32" not in blob and "x2sel" not in blob:
                    if "x2sel32" not in body.lower():
                        errors.append(
                            f"{tier} {sym} must lower via haydn_x2sel32_* "
                            "(not bag OR)"
                        )
                if body and "|" in body and "x2sel" not in body.lower():
                    errors.append(
                        f"{tier} {sym} body uses bag | without x2sel32"
                    )
        # Silent scalar i64 add under an X2 name (ADD64X2 class): only legal
        # when permanently quarantined as UNSUPPORTED. Default must not keep
        # the transitional ((a)+(b)) body.
        if re.search(r"X2", sym) and tier != "UNSUPPORTED":
            if re.search(
                r"\(\(ae_int64\)\s*\(?\s*[ab]\s*\)?\s*\+\s*\(ae_int64\)", body
            ) or re.search(
                r"\(\(ae_int64\)\s*\(\s*\([ab]\)\s*\+\s*\([ab]\)\s*\)\)", body
            ):
                errors.append(
                    f"{tier} {sym} silent scalar i64 add under X2 name "
                    "(must quarantine as UNSUPPORTED or map dual-lane)"
                )
        # Dual ASR residual (all-tier): F24/SRAI24/F32X2_SRAI/SRAS32 must
        # name x2sra32 — scalar sra/ashr under a dual name is high-lane drop.
        dual_asr = (
            "F24X2_SRAI" in sym
            or "F32X2_SRAI" in sym
            or sym in ("AE_SRAI24", "AE_SRAIP24", "AE_SRAS32")
        )
        if dual_asr and tier != "UNSUPPORTED":
            if "x2sra32" not in blob and "x2sra" not in blob:
                # Header aliases (SRAI24 → F24X2_SRAI) are fine when body
                # resolves to the dual peer.
                if "AE_F24X2_SRAI" not in body and "AE_SRAI24" not in body:
                    errors.append(
                        f"{tier} {sym} dual ASR must lower via x2sra32 "
                        "(not scalar sra/ashr)"
                    )

    # Header macro bodies for residual dual-24 aliases must resolve to dual ops.
    for sym, token in (
        ("AE_SELP24_HH", "haydn_x2sel32_hh"),
        ("AE_SELP24_HL", "haydn_x2sel32_hl"),
        ("AE_SELP24_LH", "haydn_x2sel32_lh"),
        ("AE_SELP24_LL", "haydn_x2sel32_ll"),
        ("AE_SEL24_HH", "haydn_x2sel32_hh"),
        ("AE_SEL24_HL", "haydn_x2sel32_hl"),
        ("AE_SEL24_LH", "haydn_x2sel32_lh"),
        ("AE_SEL24_LL", "haydn_x2sel32_ll"),
        ("AE_SEL32_HH", "haydn_x2sel32_hh"),
        ("AE_SEL32_HL", "haydn_x2sel32_hl"),
        ("AE_SEL32_LH", "haydn_x2sel32_lh"),
        ("AE_SEL32_LL", "haydn_x2sel32_ll"),
        ("AE_NEG24S", "haydn_x2neg32s"),
        ("AE_F24X2_SRAI", "haydn_x2sra32"),
        ("AE_F32X2_SRAI", "haydn_x2sra32"),
        ("AE_ADDP24", "haydn_x2add32"),
        ("AE_ADD24S", "haydn_x2add32s"),
        ("AE_SUB24S", "haydn_x2sub32s"),
        ("AE_SLAI24S", "__ae_slaa32s"),
        ("AE_SLAI64S", "haydn_ae_slaa64s"),
        ("AE_SLAA64S", "haydn_ae_slaa64s"),
        ("AE_ZERO24", "haydn_dr64_t"),
        ("AE_F64_SLAIS", "haydn_ae_slaa64s"),
        ("AE_F32X2_SLAIS", "__ae_slaa32s"),
        ("AE_F64_SLAS", "haydn_ae_slaa64s"),
    ):
        body = macros.get(sym, "")
        if body and token not in body:
            errors.append(f"{sym} macro body must call {token}")
    for sym, peer in (
        ("AE_SRAI24", "AE_F24X2_SRAI"),
        ("AE_SRAIP24", "AE_SRAI24"),
        ("AE_NEGSP24S", "AE_NEG24S"),
        ("AE_ADDSP24S", "AE_ADD24S"),
        ("AE_SUBSP24S", "AE_SUB24S"),
    ):
        body = macros.get(sym, "")
        if body and peer not in body and "haydn_x2" not in body:
            errors.append(f"{sym} macro body must alias {peer} or dual X2 op")
    # SLAS32S soft sat residual: late helpers must name __ae_slaa32s (not non-sat ASR).
    if "AE_SLAS32S" in macros:
        hpos = dsp.rfind("#define __AE_SLAS32S_1")
        hwin = dsp[hpos : hpos + 240] if hpos >= 0 else ""
        if "__ae_slaa32s" not in hwin and "__ae_slaa32s" not in macros.get(
            "AE_SLAS32S", ""
        ):
            errors.append(
                "AE_SLAS32S residual must route via __ae_slaa32s soft sat"
            )
    # SLAS64S may be an overload wrapper; helpers must still call haydn_ae_slaa64s.
    if "AE_SLAS64S" in macros:
        body = macros.get("AE_SLAS64S", "")
        hpos = dsp.rfind("#define __AE_SLAS64S_1")
        hwin = dsp[hpos : hpos + 240] if hpos >= 0 else ""
        if "haydn_ae_slaa64s" not in body and "haydn_ae_slaa64s" not in hwin:
            errors.append(
                "AE_SLAS64S residual must route via haydn_ae_slaa64s soft sat"
            )

    body = macros.get("AE_MAXABS16S", "") + " " + dsp
    if "maxabs32" in body.lower() and "AE_MAXABS16S" in macros:
        helper = re.findall(r"#\s*define\s+__AE_MAXABS16S_2\b.*", dsp)
        for h in helper:
            if "maxabs32" in h.lower():
                errors.append("AE_MAXABS16S helper still references maxabs32*")

    if "haydn_x4abs16s" not in dsp or "haydn_x4max16" not in dsp:
        errors.append("haydn_dsp.h missing haydn_x4abs16s/haydn_x4max16 for MAXABS16S")

    if "HAYDN_AE_UNSUPPORTED_EXPR(AE_ADD64X2_)" not in dsp:
        errors.append("AE_ADD64X2_ missing UNSUPPORTED_EXPR quarantine")
    if "HAYDN_AE_UNSUPPORTED_EXPR(AE_ADD64X2_vector)" not in dsp:
        errors.append("AE_ADD64X2_vector missing UNSUPPORTED_EXPR quarantine")

    # Default residual quarantine (last effective body): only permanent
    # dual-64 adds may expand through HAYDN_AE_UNSUPPORTED_*. Any new silent
    # alias must be either repaired (EXACT/EMULATED) or explicitly added here
    # with a product-law tier — never a quiet scalar escape under default.
    unsup_body = sorted(
        n
        for n, b in macros.items()
        if "HAYDN_AE_UNSUPPORTED" in b or "__haydn_ae_unsupported_" in b
    )
    if unsup_body != permanent_unsup:
        errors.append(
            "default UNSUPPORTED expansions must be exactly "
            f"{permanent_unsup} (got {unsup_body})"
        )
    # TD tier set and header body set must agree (no tier-without-body drift).
    if unsup_body != unsup_names:
        errors.append(
            f"UNSUPPORTED body/tier drift: bodies={unsup_body} tiers={unsup_names}"
        )
    for must in permanent_unsup:
        body = macros.get(must, "")
        if "HAYDN_AE_UNSUPPORTED_EXPR" not in body:
            errors.append(
                f"{must} effective body must be HAYDN_AE_UNSUPPORTED_EXPR "
                f"under default (got {body[:80]!r})"
            )

    # Scalar AE_ADD64 is legal EMULATED; must not be swept into dual-64 quarantine.
    if "AE_ADD64" in tiers and tiers["AE_ADD64"][0] == "UNSUPPORTED":
        errors.append(
            "AE_ADD64 (scalar) must not be UNSUPPORTED; dual-64 is ADD64X2_* only"
        )
    add64_body = macros.get("AE_ADD64", "")
    if add64_body and "HAYDN_AE_UNSUPPORTED" in add64_body:
        errors.append("AE_ADD64 effective body must not be quarantined")

    # EXACT/EMULATED macros must not expand through the UNSUPPORTED fail-closed path.
    for sym, (tier, _lowering) in tiers.items():
        if tier not in ("EXACT", "EMULATED"):
            continue
        body = macros.get(sym, "")
        if "HAYDN_AE_UNSUPPORTED" in body:
            errors.append(
                f"{tier} {sym} body routes through HAYDN_AE_UNSUPPORTED "
                "(tier/body contradiction)"
            )

    # Closed inventory equality (beyond floor): untagged/orphan already imply
    # equal sets; restate as an explicit completeness pin so shrink/grow of
    # only one side cannot pass via empty-diff accidents.
    if len(surface) != len(tiers):
        errors.append(
            f"closed inventory mismatch surface={len(surface)} tiers={len(tiers)}"
        )

    # Residual surface still defaults to EMULATED; product-law pins stay fixed.
    if tiers.get("AE_MAXABS16S", ("", ""))[0] != "EMULATED":
        errors.append("AE_MAXABS16S product law: must remain EMULATED")
    if tiers.get("AE_ADD64X2_", ("", ""))[0] != "UNSUPPORTED":
        errors.append("AE_ADD64X2_ product law: must remain UNSUPPORTED")
    if tiers.get("AE_ADD64X2_vector", ("", ""))[0] != "UNSUPPORTED":
        errors.append("AE_ADD64X2_vector product law: must remain UNSUPPORTED")

    # Soft sat-left residual class: public *S left-shift macros must not expand
    # to plain C << wrap (or non-sat SLA) under default. Covers SLAI24S /
    # SLAI64S / SLAS32S / F64_SLAIS / F32X2_SLAIS and peer sat-left names.
    # Alias-to-peer (body names another AE_*SLA*S) is allowed when the peer
    # is itself sat; bare << without slaa/sat helper is the silent miscomp.
    def is_sat_left_name(sym: str) -> bool:
        if sym in ("AE_F64_SLAIS", "AE_F32X2_SLAIS", "AE_F64_SLAS"):
            return True
        # SLAI24S / SLAA64S / SLAS32S / SLAI32S / SLAA16S / ...
        return bool(
            re.search(r"SLA[AIL]?\d+S$", sym) or re.search(r"SLAS\d+S$", sym)
        )

    for sym, (tier, _lowering) in tiers.items():
        if tier == "UNSUPPORTED" or not is_sat_left_name(sym):
            continue
        body = macros.get(sym, "")
        if not body:
            continue
        # Overload wrappers resolve via late __AE_*_1 helpers.
        scan = body
        helper = f"__{sym}_1"
        hpos = dsp.rfind(f"#define {helper}")
        if hpos >= 0:
            scan = body + " " + dsp[hpos : hpos + 240]
        if "<<" in scan and "slaa" not in scan.lower() and "sat" not in scan.lower():
            # Allow pure alias to another sat-left AE_* peer.
            if not re.search(r"\bAE_(?:F\d+(?:X\d+)?_)?SLA[AILS\d]*S\b", scan):
                errors.append(
                    f"{tier} {sym} soft sat-left residual must not plain-wrap << "
                    "(need slaa/sat helper)"
                )

    # Residual dual-24 / SEL* / F24 curated EXACT set must stay present as a
    # closed class (not only the global EXACT floor). Shrink reopens silent
    # bag-OR / high-lane-drop / plain-mem IC holes under dual names.
    residual_exact = (
        "AE_SELP24_HH",
        "AE_SELP24_HL",
        "AE_SELP24_LH",
        "AE_SELP24_LL",
        "AE_SEL24_HH",
        "AE_SEL24_HL",
        "AE_SEL24_LH",
        "AE_SEL24_LL",
        "AE_SEL32_HH",
        "AE_SEL32_HL",
        "AE_SEL32_LH",
        "AE_SEL32_LL",
        "AE_NEG24S",
        "AE_NEGSP24S",
        "AE_F24X2_SRAI",
        "AE_F32X2_SRAI",
        "AE_SRAI24",
        "AE_SRAIP24",
        "AE_ADDP24",
        "AE_ADD24S",
        "AE_SUB24S",
        "AE_ADDSP24S",
        "AE_SUBSP24S",
        "AE_ZERO24",
        "AE_LA32X2F24POS_PC",
        "AE_LA24X2POS_PC",
        "AE_LA16X4NEG_PC",
        "AE_LA32X2NEG_PC",
        "AE_LA16X4POS_PC",
        "AE_LA32X2POS_PC",
        "AE_L32X2_RIC",
        "AE_L16X4_RIC",
        "AE_L32X2F24_RIC",
        "AE_LA16X4_RIC",
        "AE_LA32X2_RIC",
        "AE_LA32X2F24_RIC",
        "AE_LA32X2F24_IC",
        "AE_SA32X2F24_IC",
        "AE_LA24X2_IC",
        "AE_SA24X2_IC",
        "AE_LA16X4_IC",
        "AE_LA32X2_IC",
        "AE_SA16X4_IC",
        "AE_SA32X2_IC",
        "AE_LA32X2F24_XC",
        "AE_SA32X2F24_XC",
        "AE_LA16X4_RIP",
        "AE_LA32X2_RIP",
        "AE_LA32X2F24_RIP",
        "AE_SA16X4_RIP",
        "AE_SA32X2_RIP",
        "AE_SA32X2F24_RIP",
        "AE_L16X4_RIP",
        "AE_L32X2_RIP",
        "AE_L32X2F24_RIP",
        "AE_S32X2_RIP",
        "AE_S32X2F24_RIP",
        "AE_LA32X2F24_IP",
        "AE_SA32X2F24_IP",
        "AE_LA24X2_IP",
        "AE_SA24X2_IP",
        "AE_L32X2_XC",
        "AE_S32X2_XC",
        "AE_L16X4_XC",
        "AE_S16X4_XC",
        "AE_L32X2F24_XC",
        "AE_S32X2F24_XC",
        "AE_SRAS32",
        "AE_SLAS32",
    )
    missing_rx = [s for s in residual_exact if tiers.get(s, ("", ""))[0] != "EXACT"]
    if missing_rx:
        errors.append(
            "residual dual-24/SEL/F24 EXACT class incomplete: "
            + ", ".join(missing_rx[:8])
        )

    # Reverse unaligned RIP residual helpers must use dir=1 UA steps, never
    # silent-alias forward IP (dir=0). Scan late overload helpers (public
    # AE_*_RIP may be an overload wrapper whose body only names the helper).
    for helper, must_have in (
        ("__AE_LA16X4_RIP_4A", ("haydn_ae_la16x4_step", ", 1)")),
        ("__AE_LA32X2_RIP_4A", ("haydn_ae_la64_step", ", 1)")),
        ("__AE_LA32X2F24_RIP_4A", ("haydn_ae_la64_step", ", 1)")),
        ("__AE_SA16X4_RIP_4A", ("haydn_ae_sa16x4_step", ", 1)")),
    ):
        win = helper_window(helper, 360)
        if not win:
            errors.append(f"missing reverse RIP residual helper {helper}")
            continue
        for tok in must_have:
            if tok not in win:
                errors.append(f"{helper} must contain reverse RIP token {tok!r}")
        if re.search(
            r"haydn_ae_(?:la16x4|la64|sa16x4|sa64)_step\([^;]*,\s*0\s*\)", win
        ):
            errors.append(f"{helper} silent-aliases forward IP (dir=0)")
    # Dual-24 reverse UA must not force ae_int32x2 assignment (type hole).
    f24_rip = helper_window("__AE_LA32X2F24_RIP_4A", 360)
    if f24_rip and "(ae_int32x2)" in f24_rip and "ae_f24x2" not in f24_rip:
        errors.append(
            "__AE_LA32X2F24_RIP_4A must keep ae_f24x2 cast (not ae_int32x2 only)"
        )
    # Dual-24 reverse/forward residual: late public bodies (last effective
    # #define) must name UA step + dir ImmArg. Overload wrappers fall back to
    # the peer helper path above.
    for sym, toks in (
        ("AE_SA32X2F24_RIP", ("haydn_ae_sa64_step", ", 1)")),
        ("AE_SA32X2_RIP", ("haydn_ae_sa64_step", ", 1)")),
        ("AE_LA32X2F24_IP", ("haydn_ae_la64_step", ", 0)")),
        ("AE_SA32X2F24_IP", ("haydn_ae_sa64_step", ", 0)")),
    ):
        win = helper_window(sym, 360)
        if not win:
            win = macros.get(sym, "")
        if not win:
            errors.append(f"missing residual body for {sym}")
            continue
        for tok in toks:
            if tok not in win:
                errors.append(f"{sym} residual body must contain {tok!r}")

    # Dual-24 aligned F24 XC residual: CB load/store with next-ptr writeback.
    # S32X2F24_XC must not silent-drop the CBR-wrapped next pointer.
    for helper, must_have in (
        ("__AE_L32X2F24_XC_4A", ("haydn_ldw_cb_imm",)),
        (
            "__AE_S32X2F24_XC_4A",
            ("haydn_sdw_cb_imm", "(ptr) =", "__np"),
        ),
        ("__AE_L32X2_XC_4A", ("haydn_ldw_cb_imm",)),
        (
            "__AE_S32X2_XC_4A",
            ("haydn_sdw_cb_imm", "(ptr) =", "__np"),
        ),
    ):
        win = helper_window(helper, 360)
        if not win:
            errors.append(f"missing aligned XC residual helper {helper}")
            continue
        for tok in must_have:
            if tok not in win:
                errors.append(f"{helper} must contain aligned XC token {tok!r}")

    # Store-finish residual: SA64POS dir=0 / SA64NEG dir=1. Unlike LA*NEG_PC
    # seed (POS alias, direction deferred to RIC/RIP), store finish owns dir.
    sa64pos = helper_window("AE_SA64POS_FP", 200)
    sa64neg = helper_window("AE_SA64NEG_FP", 200)
    if not sa64pos or "haydn_ae_sa64pos" not in sa64pos:
        errors.append("AE_SA64POS_FP must call haydn_ae_sa64pos")
    elif not re.search(r"haydn_ae_sa64pos\([^;]*,\s*0\s*\)", sa64pos):
        errors.append("AE_SA64POS_FP body must pass dir ImmArg 0")
    if not sa64neg or "haydn_ae_sa64pos" not in sa64neg:
        errors.append("AE_SA64NEG_FP must call haydn_ae_sa64pos")
    elif not re.search(r"haydn_ae_sa64pos\([^;]*,\s*1\s*\)", sa64neg):
        errors.append(
            "AE_SA64NEG_FP body must pass dir ImmArg 1 "
            "(must not silent-alias POS dir=0)"
        )
    if sa64neg and re.search(r"haydn_ae_sa64pos\([^;]*,\s*0\s*\)", sa64neg):
        errors.append("AE_SA64NEG_FP silent-aliases POS dir=0")
    # TD lowering tokens must keep dir0/dir1 distinction.
    if "AE_SA64POS_FP" in tiers:
        low = tiers["AE_SA64POS_FP"][1].lower()
        if "dir0" not in low:
            errors.append(
                f"AE_SA64POS_FP lowering is {tiers['AE_SA64POS_FP'][1]!r}, "
                "want ...dir0"
            )
    if "AE_SA64NEG_FP" in tiers:
        low = tiers["AE_SA64NEG_FP"][1].lower()
        if "dir1" not in low:
            errors.append(
                f"AE_SA64NEG_FP lowering is {tiers['AE_SA64NEG_FP'][1]!r}, "
                "want ...dir1"
            )
        if "dir0" in low:
            errors.append("AE_SA64NEG_FP lowering must not claim dir0")

    # SRAS32 residual helpers must use dual x2sra32 by SAR (not scalar ashr).
    if "AE_SRAS32" in macros:
        hpos = dsp.rfind("#define __AE_SRAS32_1")
        hwin = dsp[hpos : hpos + 240] if hpos >= 0 else ""
        if "haydn_x2sra32" not in hwin and "haydn_x2sra32" not in macros.get(
            "AE_SRAS32", ""
        ):
            errors.append(
                "AE_SRAS32 residual must route via haydn_x2sra32 dual ASR"
            )

    require("AE_TRUNCA32X2F64S", "EMULATED")
    require("AE_CVTQ56A32S", "EMULATED")
    require("AE_CVT16X4", "EMULATED")
    require("AE_CVT16X4_1ARG", "EMULATED")
    require("AE_SLAA64S", "EMULATED")
    require("AE_TRUNCA32F64S", "EMULATED")
    require_token("AE_TRUNCA32X2F64S", "satsr64")
    require_token("AE_TRUNCA32X2F64S", "pack")
    require_token("AE_CVTQ56A32S", "sext32_shl16")
    require_token("AE_CVT16X4", "x4sat32t16")
    require_token("AE_CVT16X4_1ARG", "x4sat32t16")
    require_token("AE_SLAA64S", "slaa64s")
    require_token("AE_TRUNCA32F64S", "satsr64")
    cvtq = macros.get("AE_CVTQ56A32S", "")
    if not cvtq or re.search(r"\(\s*\(\s*ae_int64\s*\)\s*0\s*\)", cvtq):
        errors.append("AE_CVTQ56A32S must not be a constant-zero body")
    if cvtq and "<<" not in cvtq:
        errors.append("AE_CVTQ56A32S body must sign-extend and shift left")
    trunc_body = inlines.get("AE_TRUNCA32X2F64S", "")
    if not trunc_body:
        errors.append("AE_TRUNCA32X2F64S public inline missing from haydn_dsp.h")
    else:
        if "shift == 32" in trunc_body or "shift==32" in trunc_body:
            errors.append("AE_TRUNCA32X2F64S must not special-case shift 32→16 FIR remap")
        if "haydn_satsr64" not in trunc_body:
            errors.append("AE_TRUNCA32X2F64S must call haydn_satsr64")
        if "unsigned" not in trunc_body:
            errors.append("AE_TRUNCA32X2F64S must pack lanes via unsigned 32-bit cast")
    cvt1 = macros.get("AE_CVT16X4_1ARG", "")
    if "haydn_x4sat32t16" not in cvt1:
        errors.append("AE_CVT16X4_1ARG must call haydn_x4sat32t16")
    if cvt1 and not re.search(r"haydn_x4sat32t16\s*\([^,]+,\s*[^)]+\)", cvt1):
        errors.append("AE_CVT16X4_1ARG must call haydn_x4sat32t16 with two arguments")
    spos = dsp.find("haydn_ae_slaa64s")
    slaa = dsp[spos:spos+700] if spos >= 0 else ""
    if not slaa:
        errors.append("haydn_ae_slaa64s soft sat helper missing")
    else:
        if "0x8000000000000000ULL) >> s" in slaa.replace(" ", ""):
            errors.append("haydn_ae_slaa64s must not compute positive minv via logical right-shift of INT64_MIN")
        if "uint64_t" not in slaa or "<<" not in slaa:
            errors.append("haydn_ae_slaa64s must use unsigned left-shift for defined overflow detection")
    for helper, utype in (
        ("haydn_ae_sla32s_lane", "uint32_t"),
        ("haydn_ae_sla16s_lane", "uint16_t"),
    ):
        hpos = dsp.find(helper)
        hwin = dsp[hpos : hpos + 500] if hpos >= 0 else ""
        if not hwin:
            errors.append(f"{helper} soft sat helper missing")
        elif utype not in hwin:
            errors.append(f"{helper} must left-shift via {utype} (defined)")
    for sym, (tier, low) in tiers.items():
        if tier in ("EXACT", "EMULATED") and low in ("", "residual", "none"):
            errors.append(f"{sym} {tier} recipe is placeholder {low!r}")
    for sym in ("AE_TRUNCA32X2F64S","AE_CVTQ56A32S","AE_CVT16X4","AE_CVT16X4_1ARG","AE_SLAA64S","AE_TRUNCA32F64S","AE_SA64POS_FP","AE_SA64NEG_FP"):
        if sym in tiers and tiers[sym][1] in ("", "residual", "none", "overload", "c-op", "header"):
            errors.append(f"{sym} P0 recipe must not be placeholder {tiers[sym][1]!r}")

    # Empty public macro body is fail-closed (availability must be explicit).
    empty_bodies = sorted(n for n, b in macros.items() if not b.strip())
    if empty_bodies:
        errors.append(
            f"{len(empty_bodies)} public AE_* macros have empty bodies (sample): "
            + ", ".join(empty_bodies[:12])
        )

    # Typed public inventory fields (availability/immediates/effects/oracle).
    fields = parse_td_oracle_fields(td)
    if len(fields) != len(tiers):
        errors.append(
            f"typed-field parse count {len(fields)} != tier count {len(tiers)}"
        )
    # Full authored OracleId floor: every EXACT/EMULATED public op must carry a
    # TD-authored OracleId (synthesis-only labels are not qualification).
    # Permanent UNSUPPORTED dual-64 ADD64X2_* stay without OracleId.
    n_oracle = sum(
        1
        for _sym, f in fields.items()
        if (f.get("OracleId") or "").strip()
    )
    # 673 inventory - 2 UNSUPPORTED = 671 authored registrations.
    if n_oracle < 671:
        errors.append(
            f"typed OracleId inventory count {n_oracle} < full EXACT+EMULATED floor 671"
        )
    n_exact_oracle = sum(
        1
        for _sym, f in fields.items()
        if f.get("tier") == "EXACT" and (f.get("OracleId") or "").strip()
    )
    if n_exact_oracle < 70:
        errors.append(
            f"EXACT OracleId count {n_exact_oracle} < EXACT floor 70"
        )
    for sym, f in fields.items():
        tier = f.get("tier")
        oid = (f.get("OracleId") or "").strip()
        if tier in ("EXACT", "EMULATED") and not oid:
            errors.append(f"{tier} {sym} missing authored OracleId")
        if tier == "UNSUPPORTED" and oid:
            errors.append(f"UNSUPPORTED {sym} must not author OracleId (got {oid!r})")
    # AE-P0 contract: non-empty OracleId + typed DirImm on store-finish.
    ae0_oracles = {
        "AE_TRUNCA32X2F64S": "ae0.trunca32x2f64s",
        "AE_TRUNCA32F64S": "ae0.trunca32f64s",
        "AE_CVTQ56A32S": "ae0.cvtq56a32s",
        "AE_CVT16X4": "ae0.cvt16x4",
        "AE_CVT16X4_1ARG": "ae0.cvt16x4_1arg",
        "AE_SLAA64S": "ae0.slaa64s",
        "AE_SA64POS_FP": "ae0.sa64pos_fp",
        "AE_SA64NEG_FP": "ae0.sa64neg_fp",
    }
    for sym, oid in ae0_oracles.items():
        if sym not in fields:
            errors.append(f"AE-P0 {sym} missing from typed TD inventory")
            continue
        got = fields[sym].get("OracleId", "")
        if got != oid:
            errors.append(
                f"AE-P0 {sym} OracleId is {got!r}, want {oid!r}"
            )
    if "AE_SA64POS_FP" in fields and fields["AE_SA64POS_FP"].get("DirImm") != "0":
        errors.append(
            f"AE_SA64POS_FP DirImm is {fields['AE_SA64POS_FP'].get('DirImm')!r}, want '0'"
        )
    if "AE_SA64NEG_FP" in fields and fields["AE_SA64NEG_FP"].get("DirImm") != "1":
        errors.append(
            f"AE_SA64NEG_FP DirImm is {fields['AE_SA64NEG_FP'].get('DirImm')!r}, want '1'"
        )
    if "AE_SA64NEG_FP" in fields and fields["AE_SA64NEG_FP"].get("MemEffect") != "1":
        errors.append("AE_SA64NEG_FP MemEffect must be 1 (store-finish)")
    if "AE_SA64POS_FP" in fields and fields["AE_SA64POS_FP"].get("MemEffect") != "1":
        errors.append("AE_SA64POS_FP MemEffect must be 1 (store-finish)")
    # Soft sat-left residual must not keep bare 'header' placeholder recipes.
    for sym in ("AE_SLAA32S", "AE_SLAA16S", "AE_SLAI32S", "AE_SLAI16S"):
        if sym in tiers and tiers[sym][1] in ("", "header"):
            errors.append(
                f"{sym} lowering is placeholder {tiers[sym][1]!r}; want soft sat token"
            )
    # Soft-sat residual public oracles (value/object evidence class).
    softsat_oracles = {
        "AE_SLAA32S": "softsat.slaa32s",
        "AE_SLAI32S": "softsat.slaa32s",
        "AE_SLAS32S": "softsat.slaa32s",
        "AE_SLAA16S": "softsat.slaa16s",
        "AE_SLAI16S": "softsat.slaa16s",
        "AE_SLAI24S": "softsat.slai24s",
        "AE_F64_SLAIS": "softsat.f64_slais",
        "AE_F64_SLAS": "softsat.f64_slais",
        "AE_F32X2_SLAIS": "softsat.f32x2_slais",
    }
    for sym, oid in softsat_oracles.items():
        if sym not in fields:
            errors.append(f"softsat residual {sym} missing from typed TD inventory")
            continue
        got = fields[sym].get("OracleId", "")
        if got != oid:
            errors.append(
                f"softsat residual {sym} OracleId is {got!r}, want {oid!r}"
            )

    # Empty-body fail-closed: EXACT/EMULATED macros must expand to a real body;
    # permanent UNSUPPORTED dual-64 must use __HAYDN_AE_UNSUPPORTED (not silent).
    for sym, body in macros.items():
        b = (body or "").strip()
        if not b or b in ("(void)0", "((void)0)", "do {} while(0)", "do { } while (0)"):
            errors.append(f"public AE macro {sym} has empty/no-op body (fail-closed required)")
    for sym in ("AE_ADD64X2_", "AE_ADD64X2_vector"):
        body = macros.get(sym, "")
        if "__HAYDN_AE_UNSUPPORTED" not in body and "unsupported" not in body.lower():
            # parse_public_ae_macros may keep only the first #define; re-scan windows.
            win = ""
            pos = dsp.rfind(f"#define {sym}")
            if pos >= 0:
                win = dsp[pos : pos + 200]
            if "__HAYDN_AE_UNSUPPORTED" not in win:
                errors.append(
                    f"{sym} must fail-closed via __HAYDN_AE_UNSUPPORTED under default"
                )

    if errors:
        print("ae-compat-tier-audit FAILED:", file=sys.stderr)
        for e in errors:
            print("  -", e, file=sys.stderr)
        print(
            f"inventory: macros={len(macros)} surface={len(surface)} td_tiers={len(tiers)}",
            file=sys.stderr,
        )
        return 1

    print(
        f"ae-compat-tier-audit OK: macros={len(macros)} surface={len(surface)} tiers={len(tiers)} "
        f"exact={n_exact} emulated={n_emu} unsupported={n_unsup}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
