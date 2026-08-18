#!/usr/bin/env python3
"""Folded public-header KPI census of haydn_dsp.h.

Measured miss only: stay one file; no family-header split; no native
FIR/FFT composite ISA. Does not author a second library matrix.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: haydn-dsp-folded-kpi-measure.py haydn_dsp.h", file=sys.stderr)
        return 2
    hdr = Path(argv[1])
    if not hdr.is_file():
        print(f"FAIL: missing {hdr}", file=sys.stderr)
        return 2
    text = hdr.read_text(errors="replace")
    if re.search(
        r"#\s*define\s+(AE_FIR_NATIVE_COMPOSITE|AE_FFT_NATIVE_COMPOSITE|"
        r"__HAYDN_DSP_FP_H|__HAYDN_DSP_HDR_SPLIT)\b",
        text,
    ):
        print("FAIL: invented native composite or family-split macro", file=sys.stderr)
        return 1
    siblings = [p.name for p in hdr.parent.glob("haydn_dsp*.h") if p.name != "haydn_dsp.h"]
    if siblings:
        print("FAIL: unapproved haydn_dsp.h split:", *siblings, file=sys.stderr)
        return 1
    ae = sorted(set(re.findall(r"\bAE_[A-Z0-9_]+\b", text)))
    fp = [n for n in ae if re.search(r"(F32|F16|F64|F24|CMUL|MULFC)", n)]
    print(f"tdsp-kpi OK: files=1 unique_AE={len(ae)} float_AE={len(fp)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
