#!/usr/bin/env python3
"""Same-artifact runtime seats for G-RUNTIME-TOOLCHAIN (T6-RT).

Pins the compiler→sysroot→consumer identity residuals that this track owns
without reviving a DecisionGuard product registry:

  * ARTIFACT.debug.step_inst contract (parcel12 preferred; same-PC residual)
  * ARTIFACT.decode Functional_Model + ARTIFACT.consumer loader/debugger
  * ARTIFACT.decode live bind (shim/model sha256 vs live files)
  * ARTIFACT.toolchain live bind (clang/llc/ld.lld/lldb + loader hashes);
    hash drift is fail-closed unbound. Restamp is producer-only
    (record_haydn_artifact_set.py --restamp). GOLDEN_INPUTS five-file
    pin count is inventory, not a product-gate fail.
  * ARTIFACT.library + ARTIFACT.product_ld required on a live stamp
  * install_product_ld binds haydn-rt/haydn.ld and refuses .bak/.broken
  * consumer working-tree bind is required by --require-consumer-install;
    committed checkout bind is residual INFO until HEAD matches
  * compile-only / identity evidence is never semantic QUALIFY
  * consumer C CoreMark/Dhrystone TARGET_BUILD_FAILED classified residual
  * sysroot hygiene (.bak / .broken debris fail-closed)
  * yarpgen frozen 28-seed corpus seat (discover only; not a fuzz gate)
  * gcc-torture float/double skip classified (T-SF4 residual, not silent)
  * T-SF5 IEEE vector conformance classified residual
  * T-SF9 op×type×symbol contract inventory (no TestFloat invent)
  * T-SF10 hygiene + frozen yarpgen count (not a fuzz gate)
  * NatureDSP residual-beyond-approved census (T-DSP3 vec_dot16 exactness)
  * NatureDSP 456-beyond / 35-approved / 491-total hifi3 census
  * T-DSP12 default-CPU + T-DSP13 declared-vs-tested residual inventory
  * llvm/utils/haydn/tdsp13_declared_vs_tested_pin.py (no 746-name harness)
  * ARTIFACT.library / sysroot libc.a+libm.a product_library_pin identity
  * M2 KPI re-sweep measured-miss (CompleteModel=0; no competitive II)
  * M6 T-ABI9 compile/link matrix sources (ret_f32/ret_f64 soft-float)
  * M10 parcel-step Instruction plugin (12-byte; no member decode)
  * M13 softfloat-in-gate (libm + overlay + classified gcc-torture skip)
  * libc strcpy/strncmp classified residual (codegen; no sysroot rebuild)
  * gcc-c-torture user-printf.c + memset-2.c + builtin-bitops-1.c +
    strlen-5.c + va-arg-1.c + va-arg-2.c library seats
    (discover-only; not QUALIFY)
  * gcc-torture CODE_IMAGE_REJECT / "direct control target is not an
    exact code record" classified residual (LLD trap-fill zeros)
  * owned helper + freestanding-shim + haydn builtins overlay presence
  * M16/M19-M22 freeze note until SF1-SF3

Usage:
  check_runtime_artifact_seats.py
  check_runtime_artifact_seats.py --llvm-src PATH --sysroot PATH
  check_runtime_artifact_seats.py --self-test
  check_runtime_artifact_seats.py --json

Exit:
  0  every required seat is present and classified
  1  seat failure
  2  usage
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
import shutil
import subprocess
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from classify_lldb_step import PARCEL_BYTES, PREFERRED  # noqa: E402
from tdsp13_declared_vs_tested_pin import check as check_tdsp13_inventory  # noqa: E402

HYGIENE_SUFFIXES = (".bak", ".broken", ".broken-t5")
HYGIENE_NAME_PARTS = (".bak", ".broken")
YARPGEN_FROZEN_COUNT = 28
DECODE_SHIM_COUNT = 7
DECODE_BIND_KINDS = ("shims", "model")
HAYDN_RT_REL = "llvm/lib/Target/Haydn/haydn-rt"
HAYDN_RT_FILES = (
    "SOFTFLOAT-CONTRACT.txt",
    "NATUREDSP-CANARY-PIN.txt",
    "PRODUCT-IDENTITY.txt",
)
HAYDN_RT_SNIPPETS = {
    "SOFTFLOAT-CONTRACT.txt": (
        "__addsf3",
        "__adddf3",
        "long double",
        "_Float16",
        "unavailable",
        "__SOFTFP__",
        "T-SF10",
        "yarpgen",
        ".bak",
        ".broken",
        "28700d57",
    ),
    "NATUREDSP-CANARY-PIN.txt": (
        "vec_scale32x32_fast_hifi3",
        "vec_shift32x32_fast_hifi3",
        "product_library_pin",
        "456",
    ),
    "PRODUCT-IDENTITY.txt": (
        "ARTIFACT.json",
        "parcel12",
        "EM_HAYDN=259",
        "decode live bind",
        "do not invent a fuzz gate",
        "install_product_ld",
        "ARTIFACT.product_ld",
        ".bak",
        ".broken",
        "user-printf.c",
        "memset-2.c",
        "builtin-bitops-1.c",
        "strlen-5.c",
        "va-arg-1.c",
        "va-arg-2.c",
        "CODE_IMAGE_REJECT",
        "direct control target is not an exact code record",
        "4b6677f8bf87",
        "28700d57",
        "T-ABI4",
        "T-ABI6",
        "T-ABI11",
        "T-ABI12",
        "i128",
        "G-ECOSYSTEM-CONSUMERS",
        "G-LIBRARY-COVERAGE",
        "G-DEBUG-OBSERVABILITY",
        "G-TEST-EVIDENCE",
    ),
}
FP_GATE_DEFINED = (
    "__addsf3",
    "__adddf3",
    "__mulsf3",
    "__muldf3",
    "__extendsfdf2",
    "__truncdfsf2",
)
FP_GATE_ARCHIVES = (
    "libclang_rt.builtins-haydn.a",
    "libclang_rt.builtins.a",
)
TORTURE_FP_MARKERS = (
    "[^a-zA-Z0-9_]float[^a-zA-Z0-9_]",
    "[^a-zA-Z0-9_]double[^a-zA-Z0-9_]",
)
FREEZE_IDS = ("M16", "M19", "M20", "M21", "M22")
FREEZE_NEEDLE = "SF1-SF3"
OWNED_HELPERS = (
    "llvm/utils/haydn/write_ci_verdict.py",
    "llvm/utils/haydn/classify_lldb_step.py",
    "llvm/utils/haydn/run_abi_conformance_matrix.sh",
    "llvm/utils/haydn/check_runtime_artifact_seats.sh",
    "llvm/utils/haydn/check_xfail_ledger.py",
    "llvm/utils/haydn/parse_lit_summary.py",
    "llvm/utils/haydn/product_coverage_pin.sh",
    "llvm/utils/haydn/record_haydn_artifact_set.py",
    "llvm/utils/haydn/check_product_ld.py",
    "llvm/utils/haydn/tdsp13_declared_vs_tested_pin.py",
    "llvm/utils/haydn/abi-conformance/callee.c",
    "llvm/utils/haydn/abi-conformance/caller.c",
    "llvm/utils/haydn/abi-conformance/atomics_symbols.c",
    "llvm/utils/haydn/abi-conformance/start.c",
)
INSTRUCTION_PLUGIN = (
    "lldb/source/Plugins/Instruction/Haydn/EmulateInstructionHaydn.cpp"
)
INSTRUCTION_HEADER = (
    "lldb/source/Plugins/Instruction/Haydn/EmulateInstructionHaydn.h"
)
INSTRUCTION_NEEDLES = (
    "kFormatEParcelBytes",
    "No member decode",
    "when PC is unchanged",
)
CLANG_HAYDN_DRIVER = "clang/lib/Driver/ToolChains/Haydn.cpp"
CLANG_HAYDN_NEEDLES = ("--nmagic", "-lm")
TOOLCHAIN_BIND_TOOLS = ("clang", "llc", "ld.lld", "lldb")
TSF9_CONTRACT_NEEDLES = (
    "T-SF9",
    "__addsf3",
    "__adddf3",
    "long double",
    "_Float16",
    "T-SF5",
    "T-SF10",
)
SOFTFLOAT_LIBCALL_PIN = "llvm/test/CodeGen/Haydn/soft-float-libcalls.ll"
BUILTINS_OVERLAY = "compiler-rt/lib/builtins/haydn/fp_mode.c"
AUTHORITY_ANCHORS = "llvm/test/CodeGen/Haydn/Inputs/SOURCE-AUTHORITY-ANCHORS.txt"
MEASURE_SCHED = "llvm/utils/haydn/measure_sched_artifact.py"
ABI_MATRIX_SRCS = (
    "llvm/utils/haydn/abi-conformance/callee.c",
    "llvm/utils/haydn/abi-conformance/caller.c",
    "llvm/utils/haydn/abi-conformance/atomics_symbols.c",
    "llvm/utils/haydn/abi-conformance/start.c",
)
SYSROOT_LIBS = ("libc.a", "libm.a")
APPROVED_EXPAND_COUNT = 35
BEYOND_APPROVED_COUNT = 456
TOTAL_HIFI3_COUNT = 491
LIBRARY_TORTURE_SEATS = (
    "user-printf.c",
    "memset-2.c",
    "builtin-bitops-1.c",
    "strlen-5.c",
    "va-arg-1.c",
    "va-arg-2.c",
)
STALE_FULL_GATE_COMMIT = "28700d57"
REBIND_ANCESTOR = "4b6677f8bf87"
IMAGE_REJECT_NEEDLE = "CODE_IMAGE_REJECT"
IMAGE_REJECT_MSG = "direct control target is not an exact code record"
LLD_HAYDN = "lld/ELF/Arch/Haydn.cpp"
LLD_CONTROL_NEEDLES = (
    "trapInstr = {0x00, 0x00, 0x00, 0x00}",
    "CODE_IMAGE_REJECT",
    IMAGE_REJECT_MSG,
    "nopFiller",
)
OBJECT_PROFILE_TESTS = (
    "lld/test/ELF/haydn/eflags-reject-zero.s",
    "lld/test/ELF/haydn/trap-fill-not-zero.s",
    "lld/test/ELF/haydn/product-ld-bind.s",
)
CODEGEN_EFLAGS_TEST = "llvm/test/CodeGen/Haydn/eflags-e96-product-profile.ll"
ELF_WRITER = "llvm/lib/Target/Haydn/MCTargetDesc/HaydnELFObjectWriter.cpp"
TARGET_INFO = "llvm/lib/Target/Haydn/TargetInfo/HaydnTargetInfo.cpp"
EM_HAYDN_WRITER_NEEDLES = (
    "ELF::EM_HAYDN",
    "static_assert(ELF::EM_HAYDN == 259",
    "KVX",
    "do not invent",
)
EM_HAYDN_TARGETINFO_NEEDLES = (
    "EM_HAYDN=259",
    "KVX",
    "Do not invent a replacement",
)
FREESTANDING_SHIMS = (
    "alloca.h",
    "assert.h",
    "complex.h",
    "errno.h",
    "fenv.h",
    "inttypes.h",
    "math.h",
    "stdlib.h",
    "string.h",
    "restore_castxcc.h",
    "ndsp_private_overlay/common.h",
    "xtensa/config/core-isa.h",
    "xtensa/tie/xt_core.h",
    "xtensa/tie/xt_hifi3.h",
    "xtensa/tie/xt_misc.h",
)


def monorepo_from_script() -> Path:
    return Path(__file__).resolve().parents[3]


def discover_sysroot(explicit: Optional[Path], haydn_bin: Optional[Path]) -> Optional[Path]:
    candidates: List[Path] = []
    if explicit:
        candidates.append(explicit)
    for env_name in ("BUNDLESIM_SYSROOT", "HAYDN_SYSROOT"):
        val = os.environ.get(env_name)
        if val:
            candidates.append(Path(val))
    if haydn_bin:
        candidates.append(haydn_bin.parent / "sysroot" / "haydn-unknown-elf")
    default_bin = os.environ.get("HAYDN_BIN")
    if default_bin:
        candidates.append(Path(default_bin).parent / "sysroot" / "haydn-unknown-elf")
    candidates.append(Path("/ssd2/mhyang/haydn-build/sysroot/haydn-unknown-elf"))
    for cand in candidates:
        if (cand / "ARTIFACT.json").is_file():
            return cand.resolve()
    return None


def discover_bundlesim(explicit: Optional[Path]) -> Optional[Path]:
    if explicit and explicit.is_dir():
        return explicit.resolve()
    env = os.environ.get("BUNDLESIM_ROOT")
    if env and Path(env).is_dir():
        return Path(env).resolve()
    default = Path("/ssd2/mhyang/BundleSim")
    if default.is_dir():
        return default
    return None


def discover_naturedsp_tools(llvm_src: Path) -> Optional[Path]:
    env = os.environ.get("TOOLS")
    if env and Path(env).is_dir():
        return Path(env).resolve()
    plans = Path("/ssd2/mhyang/haydn-plans/naturedsp-haydn/tools")
    if plans.is_dir():
        return plans
    return None


def discover_torture_execute(explicit: Optional[Path] = None) -> Optional[Path]:
    """gcc-c-torture/execute root. Classification only; do not run the suite."""
    candidates: List[Path] = []
    if explicit is not None:
        candidates.append(explicit)
    for env_name in (
        "HAYDN_TORTURE_EXECUTE",
        "TORTURE_SRC",
        "BUNDLESIM_LLVM_TESTSUITE_SRC",
        "LLVM_TESTSUITE",
    ):
        val = os.environ.get(env_name)
        if val:
            candidates.append(Path(val))
    candidates.extend(
        (
            Path("/ssd/mhyang/llvm/llvm-testsuite"),
            Path("/ssd/mhyang/llvm/gcc-12/gcc/testsuite/gcc.c-torture/execute"),
        )
    )
    def _has_any_seat(root: Path) -> bool:
        return any((root / name).is_file() for name in LIBRARY_TORTURE_SEATS)

    for cand in candidates:
        if _has_any_seat(cand):
            return cand.resolve()
        nested = cand / "SingleSource/Regression/C/gcc-c-torture/execute"
        if _has_any_seat(nested):
            return nested.resolve()
    return None


def load_json(path: Path) -> Dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _false_qualify(block: Any) -> bool:
    if not isinstance(block, dict):
        return False
    return block.get("qualified") is True or block.get("semantic_qualify") is True


def check_debug_step_contract(artifact: Path, errs: List[str]) -> None:
    doc = load_json(artifact)
    debug = doc.get("debug") or {}
    if not debug:
        errs.append("artifact.debug.missing")
        return
    if not debug.get("complete"):
        errs.append("artifact.debug.incomplete")
    if int(debug.get("parcel_bytes") or 0) != PARCEL_BYTES:
        errs.append("artifact.debug.parcel_bytes")
    step = debug.get("step_inst") or {}
    if step.get("preferred_class") != PREFERRED:
        errs.append("artifact.debug.step.preferred_class")
    if step.get("same_pc_class") != "residual":
        errs.append("artifact.debug.step.same_pc_unclassified")
    if step.get("non12_delta_class") != "residual":
        errs.append("artifact.debug.step.non12_unclassified")
    if step.get("qualified") is True:
        errs.append("artifact.debug.step.false_qualified")
    eflags = debug.get("eflags") or {}
    if eflags.get("external_allocation") is True:
        errs.append("artifact.debug.eflags.external_allocation")
    if eflags.get("policy") != "provisional-consumer-agreement":
        errs.append("artifact.debug.eflags.policy")
    if int(eflags.get("value") or 0) != 1:
        errs.append("artifact.debug.eflags.value")
    ns = debug.get("register_namespaces") or {}
    if ns.get("do_not_equate") is False:
        errs.append("artifact.debug.register_namespaces.equated")


def check_decode_contract(artifact: Path, errs: List[str]) -> None:
    """Identity of Functional_Model shims/models. Not semantic QUALIFY."""
    doc = load_json(artifact)
    dec = doc.get("decode") or {}
    if not dec:
        errs.append("artifact.decode.missing")
        return
    if dec.get("authority") != "five-file":
        errs.append("artifact.decode.authority")
    if not dec.get("complete"):
        errs.append("artifact.decode.incomplete")
    if not dec.get("shim_discipline_ok"):
        errs.append("artifact.decode.shim_discipline")
    if int(dec.get("shim_count") or 0) != DECODE_SHIM_COUNT:
        errs.append("artifact.decode.shim_count")
    if int(dec.get("model_count") or 0) != DECODE_SHIM_COUNT:
        errs.append("artifact.decode.model_count")
    if _false_qualify(dec):
        errs.append("artifact.decode.false_qualified")


def _decode_named_files(dec: Dict[str, Any], kind: str) -> Dict[str, Dict[str, Any]]:
    block = dec.get(kind) or {}
    return block if isinstance(block, dict) else {}


def check_decode_bind(artifact: Path, errs: List[str], infos: List[str]) -> None:
    """Live Functional_Model + model hashes must match ARTIFACT.decode."""
    doc = load_json(artifact)
    dec = doc.get("decode") or {}
    if not dec:
        return
    unbound = 0
    for kind in DECODE_BIND_KINDS:
        block = _decode_named_files(dec, kind)
        if not block:
            errs.append(f"artifact.decode.{kind}.missing")
            continue
        if len(block) != DECODE_SHIM_COUNT:
            errs.append(f"artifact.decode.{kind}.count")
        for name, meta in block.items():
            if not isinstance(meta, dict):
                errs.append(f"artifact.decode.{kind}.{name}.meta")
                unbound += 1
                continue
            path = Path(str(meta.get("path") or ""))
            digest = str(meta.get("sha256") or "")
            if not path.is_file():
                errs.append(f"artifact.decode.{kind}.{name}.missing")
                unbound += 1
                continue
            if len(digest) != 64:
                errs.append(f"artifact.decode.{kind}.{name}.unstamped")
                unbound += 1
                continue
            if file_sha256(path) != digest:
                infos.append(
                    f"artifact.decode.{kind}.{name}.unbound "
                    "(live file hash differs from ARTIFACT pin; not a compiler fail)"
                )
                unbound += 1
    if unbound == 0 and not any(
        e.startswith("artifact.decode.") and e.endswith((".missing", ".count"))
        for e in errs
    ):
        infos.append("decode live bind: Functional_Model + model hashes match")


def _discover_llvm_nm(haydn_bin: Optional[Path]) -> Optional[Path]:
    if haydn_bin is not None:
        cand = haydn_bin / "llvm-nm"
        if cand.is_file():
            return cand
    which = shutil.which("llvm-nm")
    return Path(which) if which else None


def parse_nm_defined(text: str) -> Set[str]:
    """Defined (T/t/D/d/W/w) symbol names from llvm-nm output."""
    found: Set[str] = set()
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        # "T name" or "hex T name"
        if len(parts) >= 3 and parts[-2] in "TtDdWwRrBb":
            found.add(parts[-1])
        elif len(parts) == 2 and parts[0] in "TtDdWwRrBb":
            found.add(parts[1])
    return found


def check_fp_in_gate(
    sysroot: Optional[Path],
    haydn_bin: Optional[Path],
    errs: List[str],
    infos: List[str],
) -> None:
    """T-SF4: compiler-rt on the matching sysroot defines IEEE helpers."""
    if sysroot is None:
        return
    lib = sysroot / "lib"
    archives = [lib / name for name in FP_GATE_ARCHIVES if (lib / name).is_file()]
    if not archives:
        infos.append("T-SF4 FP-in-gate skipped (no compiler-rt builtins archive)")
        return
    nm = _discover_llvm_nm(haydn_bin)
    if nm is None:
        infos.append("T-SF4 FP-in-gate skipped (no llvm-nm)")
        return
    defined: Set[str] = set()
    for archive in archives:
        try:
            proc = subprocess.run(
                [str(nm), str(archive)],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
            )
        except OSError:
            infos.append("T-SF4 FP-in-gate skipped (llvm-nm unusable)")
            return
        if proc.returncode != 0:
            infos.append("T-SF4 FP-in-gate skipped (llvm-nm failed to run)")
            return
        defined |= parse_nm_defined(proc.stdout or "")
    missing = [name for name in FP_GATE_DEFINED if name not in defined]
    if missing:
        errs.append("t-sf4.fp_in_gate.missing:" + ",".join(missing))
        return
    infos.append(
        "T-SF4 FP-in-gate: compiler-rt defines add/mul/convert helpers"
    )


def check_haydn_rt_contracts(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    runtime = llvm_src / HAYDN_RT_REL
    if not runtime.is_dir():
        if (llvm_src / "llvm" / "lib" / "Target" / "Haydn").is_dir():
            errs.append("haydn-rt.contracts.missing")
        return
    missing_files: List[str] = []
    missing_snip: List[str] = []
    for name in HAYDN_RT_FILES:
        path = runtime / name
        if not path.is_file():
            missing_files.append(name)
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        folded = text.casefold()
        for snippet in HAYDN_RT_SNIPPETS.get(name, ()):
            if snippet.casefold() not in folded:
                missing_snip.append(f"{name}:{snippet}")
    if missing_files:
        errs.append("haydn-rt.contracts.files:" + ",".join(missing_files))
        return
    if missing_snip:
        errs.append("haydn-rt.contracts.snippets:" + ",".join(missing_snip[:6]))
        return
    infos.append("haydn-rt contract pins present (M1/M10/M13/M15 leftover)")


def check_consumer_contract(artifact: Path, errs: List[str]) -> None:
    """Loader + debugger hashes on the same ARTIFACT tuple. Not QUALIFY."""
    doc = load_json(artifact)
    cons = doc.get("consumer") or {}
    if not cons:
        errs.append("artifact.consumer.missing")
        return
    if not cons.get("complete"):
        errs.append("artifact.consumer.incomplete")
    dbg = cons.get("debugger") or {}
    loader = cons.get("loader") or {}
    if not dbg.get("sha256"):
        errs.append("artifact.consumer.debugger.missing")
    if dbg.get("role") not in (None, "lldb"):
        errs.append("artifact.consumer.debugger.role")
    if not loader.get("sha256"):
        errs.append("artifact.consumer.loader.missing")
    if _false_qualify(cons):
        errs.append("artifact.consumer.false_qualified")


def check_no_semantic_qualify(artifact: Path, errs: List[str]) -> None:
    """Compile/identity evidence must never be labeled semantic QUALIFY."""
    doc = load_json(artifact)
    if _false_qualify(doc):
        errs.append("artifact.semantic_qualify.false_green")


def _live_tool_path(block: Any, fallback: Optional[Path]) -> Optional[Path]:
    if isinstance(block, dict):
        stamped = block.get("path")
        if stamped and Path(stamped).is_file():
            return Path(stamped)
    if fallback is not None and fallback.exists():
        return fallback.resolve()
    return None


def check_golden_inputs_pin_not_gate(artifact: Path, infos: List[str]) -> None:
    """Catalog GOLDEN_INPUTS pin count is inventory, never a product fail."""
    cat = (load_json(artifact).get("catalog") or {})
    pin = cat.get("golden_inputs_pin") or {}
    if not isinstance(pin, dict) or not pin:
        return
    count = int(pin.get("file_count") or 0)
    infos.append(
        f"GOLDEN_INPUTS pin file_count={count} "
        "(five-file pin is not a product-gate fail)"
    )


def check_toolchain_bind(
    artifact: Path,
    haydn_bin: Optional[Path],
    bundlesim: Optional[Path],
    errs: List[str],
    infos: List[str],
) -> None:
    """Fail-closed when stamped tool hashes no longer match live binaries.

    Hash drift is unbound. Restamp is producer-only
    (record_haydn_artifact_set.py --restamp). GOLDEN_INPUTS five-file
    pin count is not checked here.
    """
    doc = load_json(artifact)
    toolchain = doc.get("toolchain") or {}
    if not toolchain:
        errs.append("artifact.toolchain.missing")
        return
    if _false_qualify(toolchain):
        errs.append("artifact.toolchain.false_qualified")
    for name in TOOLCHAIN_BIND_TOOLS:
        block = toolchain.get(name) or {}
        if name == "ld.lld" and not (
            isinstance(block, dict) and block.get("sha256")
        ):
            block = toolchain.get("lld") or block
        digest = str(block.get("sha256") or "") if isinstance(block, dict) else ""
        if len(digest) != 64:
            errs.append(f"artifact.toolchain.{name}.missing")
            continue
        fallback = None
        if haydn_bin is not None:
            if name == "ld.lld":
                for cand in ("ld.lld", "lld"):
                    if (haydn_bin / cand).exists():
                        fallback = haydn_bin / cand
                        break
            else:
                fallback = haydn_bin / name
        live = _live_tool_path(block, fallback)
        if live is None:
            infos.append(f"artifact.toolchain.{name} stamped; live binary not discovered")
            continue
        if file_sha256(live) != digest:
            errs.append(f"artifact.toolchain.{name}.unbound")
    cons = doc.get("consumer") or {}
    loader = cons.get("loader") or {}
    loader_hash = str(loader.get("sha256") or "")
    if len(loader_hash) == 64:
        loader_fallback = None
        if bundlesim is not None:
            cand = bundlesim / "build" / "BundleSim"
            if cand.is_file():
                loader_fallback = cand
        live_loader = _live_tool_path(loader, loader_fallback)
        if live_loader is not None and file_sha256(live_loader) != loader_hash:
            infos.append(
                "artifact.consumer.loader.unbound "
                "(live BundleSim hash differs from ARTIFACT pin)"
            )
    dbg = cons.get("debugger") or {}
    dbg_hash = str(dbg.get("sha256") or "")
    if len(dbg_hash) == 64:
        dbg_fallback = (haydn_bin / "lldb") if haydn_bin is not None else None
        live_dbg = _live_tool_path(dbg, dbg_fallback)
        if live_dbg is not None and file_sha256(live_dbg) != dbg_hash:
            errs.append("artifact.consumer.debugger.unbound")


def check_consumer_c_residual(
    llvm_src: Path, bundlesim: Optional[Path], errs: List[str], infos: List[str]
) -> None:
    """CoreMark/Dhrystone consumer-C compile kill is named, never QUALIFY."""
    anchors = llvm_src / AUTHORITY_ANCHORS
    if anchors.is_file():
        text = anchors.read_text(encoding="utf-8", errors="replace")
        lowered = text.lower()
        if "TARGET_BUILD_FAILED" not in text or "consumer c" not in lowered:
            errs.append("consumer_c.residual.unclassified")
            return
    elif (llvm_src / "llvm" / "utils" / "haydn").is_dir():
        errs.append("consumer_c.residual.anchors.missing")
        return
    if bundlesim is not None:
        for rel, tag in (
            ("bundlesim/tests/campaigns/run_coremark_qualification.py", "coremark"),
            ("bundlesim/tests/campaigns/run_dhrystone_qualification.py", "dhrystone"),
        ):
            path = bundlesim / rel
            if not path.is_file():
                errs.append(f"consumer_c.{tag}.harness.missing")
                continue
            body = path.read_text(encoding="utf-8", errors="replace")
            if "TARGET_BUILD_FAILED" not in body:
                errs.append(f"consumer_c.{tag}.status_unclassified")
    infos.append(
        "consumer C CoreMark/Dhrystone TARGET_BUILD_FAILED classified residual"
    )


def check_ieee_residual(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """T-SF5 IEEE vector conformance stays a named residual."""
    anchors = llvm_src / AUTHORITY_ANCHORS
    if anchors.is_file():
        text = anchors.read_text(encoding="utf-8", errors="replace")
        if "T-SF5" not in text or "IEEE" not in text:
            errs.append("t-sf5.ieee.unclassified")
            return
    infos.append("T-SF5 IEEE vector conformance classified residual")


def check_tsf9_contract(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """T-SF9 op×type×symbol contract is inventory, not a TestFloat suite."""
    anchors = llvm_src / AUTHORITY_ANCHORS
    if not anchors.is_file():
        if (llvm_src / "llvm" / "utils" / "haydn").is_dir():
            errs.append("t-sf9.contract.anchors.missing")
        return
    text = anchors.read_text(encoding="utf-8", errors="replace")
    missing = [n for n in TSF9_CONTRACT_NEEDLES if n not in text]
    if missing:
        errs.append("t-sf9.contract.unclassified:" + ",".join(missing))
        return
    if "TestFloat" in text and "do not invent TestFloat" not in text:
        errs.append("t-sf9.contract.testfloat_invent")
        return
    pin = llvm_src / SOFTFLOAT_LIBCALL_PIN
    if pin.is_file():
        pin_text = pin.read_text(encoding="utf-8", errors="replace")
        if "__addsf3" not in pin_text:
            errs.append("t-sf9.contract.libcall_pin")
            return
    elif (llvm_src / "llvm" / "test" / "CodeGen" / "Haydn").is_dir():
        errs.append("t-sf9.contract.libcall_pin.missing")
        return
    topic = Path("/ssd2/mhyang/haydn-plans/topics/softfloat/TOPIC.md")
    if topic.is_file():
        topic_text = topic.read_text(encoding="utf-8", errors="replace")
        if "T-SF9" not in topic_text or "__addsf3" not in topic_text:
            errs.append("t-sf9.contract.topic_stale")
            return
    infos.append("T-SF9 softfloat contract inventoried (no TestFloat invent)")


def check_product_ld_install_path(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """Monorepo recorder must bind haydn-rt/haydn.ld and refuse .bak/.broken."""
    recorder = llvm_src / "llvm" / "utils" / "haydn" / "record_haydn_artifact_set.py"
    if not recorder.is_file():
        if (llvm_src / "llvm" / "utils" / "haydn").is_dir():
            errs.append("product_ld.install_path.missing")
        return
    text = recorder.read_text(encoding="utf-8", errors="replace")
    if "def install_product_ld" not in text:
        errs.append("product_ld.install_path.missing")
        return
    if "haydn-rt/haydn.ld" not in text:
        errs.append("product_ld.install_bind.missing")
        return
    if ".bak" not in text or ".broken" not in text:
        errs.append("product_ld.hygiene.refuse.missing")
        return
    infos.append("product-ld install path: haydn-rt/haydn.ld + .bak/.broken refuse")


def check_consumer_install_script(
    bundlesim: Optional[Path],
    errs: List[str],
    infos: List[str],
    *,
    require: bool = False,
) -> None:
    """BundleSim install writes haydn-rt/haydn.ld and refuses .bak/.broken.

    Default is residual INFO for the committed checkout. require=True
    fail-closes on the owned working-tree script (impl-track land).
    HEAD-unbound stays INFO until the consumer checkout identity matches.
    """
    def _note(msg: str) -> None:
        if require:
            errs.append(msg)
        else:
            infos.append("T7-RT residual (OPEN): " + msg)

    if bundlesim is None:
        infos.append("consumer install script skipped (BundleSim not discoverable)")
        return
    path = bundlesim / "scripts" / "install_haydn_sysroot.sh"
    if not path.is_file():
        _note("consumer.install_haydn_sysroot.missing")
        return
    text = path.read_text(encoding="utf-8", errors="replace")
    if "haydn-rt/haydn.ld" not in text:
        _note("consumer.install.product_ld.bind.missing")
    elif "PRODUCT_LD" not in text and "_product_ld" not in text:
        _note("consumer.install.product_ld.path.missing")
    elif ".bak" not in text or ".broken" not in text:
        _note("consumer.install.hygiene.refuse.missing")
    else:
        infos.append(
            "consumer install (working tree): haydn-rt/haydn.ld + .bak/.broken refuse"
        )
        if require:
            if "--self-test" not in text or "_self_test_product_ld" not in text:
                errs.append("consumer.install.self_test.missing")
            else:
                try:
                    out = subprocess.check_output(
                        ["bash", str(path), "--self-test"],
                        stderr=subprocess.STDOUT,
                        text=True,
                        timeout=30,
                    )
                except (
                    OSError,
                    subprocess.CalledProcessError,
                    subprocess.TimeoutExpired,
                ) as exc:
                    detail = ""
                    if isinstance(exc, subprocess.CalledProcessError) and exc.output:
                        detail = ":" + exc.output.strip().splitlines()[-1]
                    errs.append("consumer.install.self_test.failed" + detail)
                else:
                    if "product-ld self-test OK" not in out:
                        errs.append("consumer.install.self_test.no_ok")
                    else:
                        infos.append(
                            "consumer install --self-test: refuse + bind OK"
                        )

    committed: Optional[str] = None
    try:
        committed = subprocess.check_output(
            [
                "git",
                "-C",
                str(bundlesim),
                "show",
                "HEAD:scripts/install_haydn_sysroot.sh",
            ],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        committed = None
    if committed is None:
        infos.append("T7-RT residual (OPEN): committed install script unread")
    elif "haydn-rt/haydn.ld" not in committed or (
        "PRODUCT_LD" not in committed and "_product_ld" not in committed
    ) or ".bak" not in committed or ".broken" not in committed:
        infos.append(
            "T7-RT residual (OPEN): committed install_haydn_sysroot.sh unbound"
        )
    else:
        infos.append("T7-RT committed install binds haydn-rt/haydn.ld")


def _git_work_tree(repo: Path) -> bool:
    try:
        out = subprocess.check_output(
            ["git", "-C", str(repo), "rev-parse", "--is-inside-work-tree"],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return False
    return out.strip() == "true"


def git_commit_is_repo_resident(repo: Path, commit: str) -> bool:
    """True when commit exists and is an ancestor of HEAD (not 28700d57)."""
    if not commit or commit.lower().startswith(STALE_FULL_GATE_COMMIT):
        return False
    if not _git_work_tree(repo):
        return False
    try:
        subprocess.check_output(
            ["git", "-C", str(repo), "cat-file", "-t", commit],
            stderr=subprocess.DEVNULL,
        )
        subprocess.check_call(
            ["git", "-C", str(repo), "merge-base", "--is-ancestor", commit, "HEAD"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return False
    return True


def check_artifact_rebind(
    artifact: Optional[Path],
    errs: List[str],
    infos: List[str],
    llvm_src: Optional[Path] = None,
) -> None:
    """Post-wave rebind: refuse 28700d57; live commit must be repo-resident."""
    if artifact is None or not artifact.is_file():
        return
    doc = load_json(artifact)
    llvm_block = doc.get("llvm_src") or {}
    commit = ""
    if isinstance(llvm_block, dict):
        commit = str(llvm_block.get("git_commit") or "")
    elif isinstance(llvm_block, str):
        commit = llvm_block
    if not commit:
        errs.append("artifact.llvm_src.git_commit.missing")
        return
    if commit.lower().startswith(STALE_FULL_GATE_COMMIT):
        errs.append(
            "artifact.llvm_src.git_commit.stale_28700d57 "
            f"(rebind to in-repo {REBIND_ANCESTOR})"
        )
        return
    repo: Optional[Path] = None
    if llvm_src is not None and _git_work_tree(llvm_src):
        repo = llvm_src
    if repo is not None and not git_commit_is_repo_resident(repo, commit):
        errs.append(
            "artifact.llvm_src.git_commit.not_repo_resident "
            f"({commit}; refuse {STALE_FULL_GATE_COMMIT}; "
            f"pin {REBIND_ANCESTOR})"
        )
        return
    infos.append(
        f"artifact rebound: git_commit={commit} "
        f"(refuse {STALE_FULL_GATE_COMMIT}; pin {REBIND_ANCESTOR})"
    )


def check_torture_image_reject(
    llvm_src: Path,
    bundlesim: Optional[Path],
    errs: List[str],
    infos: List[str],
) -> None:
    """gcc-torture CODE_IMAGE_REJECT is residual, never QUALIFY."""
    identity = llvm_src / HAYDN_RT_REL / "PRODUCT-IDENTITY.txt"
    if identity.is_file():
        text = identity.read_text(encoding="utf-8", errors="replace")
        if IMAGE_REJECT_NEEDLE not in text:
            errs.append("torture.image_loader.reject.unclassified")
            return
        if IMAGE_REJECT_MSG not in text:
            errs.append("torture.image_loader.reject.msg.unclassified")
            return
    elif (llvm_src / "llvm" / "lib" / "Target" / "Haydn").is_dir():
        errs.append("torture.image_loader.reject.identity.missing")
        return
    if bundlesim is not None:
        seats = (
            bundlesim / "bundlesim/src/frontend/frontend_pipeline.c",
            bundlesim / "bundlesim/tests/unit/test_frontend_pipeline.c",
            bundlesim / "bundlesim/src/frontend/code_image_io.c",
        )
        found = False
        for path in seats:
            if not path.is_file():
                continue
            body = path.read_text(encoding="utf-8", errors="replace")
            if IMAGE_REJECT_NEEDLE in body:
                found = True
                break
        if not found:
            infos.append(
                "T7-RT residual: consumer CODE_IMAGE_REJECT pin unread"
            )
        else:
            infos.append(
                "torture image/loader CODE_IMAGE_REJECT classified residual "
                f"({IMAGE_REJECT_MSG}; not QUALIFY; no torture run)"
            )
            return
    infos.append(
        "torture image/loader CODE_IMAGE_REJECT classified residual "
        f"({IMAGE_REJECT_MSG}; not QUALIFY; no torture run)"
    )


def check_lld_control_target(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """LLD trap-fill zeros keep control targets on exact code records."""
    path = llvm_src / LLD_HAYDN
    if not path.is_file():
        infos.append("LLD Haydn control-target pin skipped (no lld tree)")
        return
    text = path.read_text(encoding="utf-8", errors="replace")
    missing = [n for n in LLD_CONTROL_NEEDLES if n not in text]
    if missing:
        errs.append("lld.control_target.unclassified:" + ",".join(missing[:4]))
        return
    infos.append(
        "LLD trap-fill zeros: "
        + IMAGE_REJECT_MSG
        + " classified (not QUALIFY)"
    )


def check_product_ld_identity(artifact: Optional[Path], llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """In-tree haydn.ld must be stamped on the same ARTIFACT tuple."""
    product = llvm_src / "haydn-rt" / "haydn.ld"
    if not product.is_file():
        infos.append("product_ld skipped (haydn-rt/haydn.ld not in tree)")
        return
    if artifact is None or not artifact.is_file():
        return
    doc = load_json(artifact)
    block = doc.get("product_ld") or {}
    if not block:
        errs.append(
            "artifact.product_ld.missing "
            "(null is fail-closed; bind with "
            "record_haydn_artifact_set.py --install-product-ld)"
        )
        return
    if (block.get("authority") or "") != "haydn-rt/haydn.ld":
        errs.append("artifact.product_ld.authority")
    if block.get("complete") is False or block.get("body_match") is False:
        errs.append("artifact.product_ld.body_mismatch")
    if _false_qualify(block):
        errs.append("artifact.product_ld.false_qualified")
    infos.append("product_ld identity: haydn-rt/haydn.ld stamped")


def check_builtins_overlay(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    overlay = llvm_src / BUILTINS_OVERLAY
    builtins_root = llvm_src / "compiler-rt" / "lib" / "builtins"
    if not overlay.is_file():
        if builtins_root.is_dir():
            errs.append("compiler-rt.haydn.fp_mode.missing")
        else:
            infos.append("compiler-rt haydn overlay skipped (no compiler-rt tree)")
        return
    text = overlay.read_text(encoding="utf-8", errors="replace")
    if "CRT_FE_TONEAREST" not in text or "__fe_getround" not in text:
        errs.append("compiler-rt.haydn.fp_mode.incomplete")
        return
    infos.append("compiler-rt haydn fp_mode overlay present (soft-float default)")


def check_sysroot_hygiene(sysroot: Path, errs: List[str]) -> List[str]:
    found: List[str] = []
    lib = sysroot / "lib"
    roots = [lib] if lib.is_dir() else [sysroot]
    for root in roots:
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            name = path.name
            if name.endswith(HYGIENE_SUFFIXES) or any(part in name for part in HYGIENE_NAME_PARTS):
                found.append(str(path.relative_to(sysroot)))
    if found:
        errs.append("sysroot.hygiene:" + ",".join(sorted(found)[:8]))
    return found


def check_yarpgen_seat(bundlesim: Optional[Path], errs: List[str], infos: List[str]) -> None:
    if bundlesim is None:
        infos.append("yarpgen.seat skipped (BundleSim not discoverable)")
        return
    manifest = bundlesim / "bundlesim/tests/regression/cases/YARPGEN_MANIFEST.json"
    if not manifest.is_file():
        errs.append("yarpgen.manifest.missing")
        return
    doc = load_json(manifest)
    count = int(doc.get("count") or 0)
    cases = doc.get("cases") or []
    if count != YARPGEN_FROZEN_COUNT or len(cases) != YARPGEN_FROZEN_COUNT:
        errs.append(f"yarpgen.frozen_count!={YARPGEN_FROZEN_COUNT}")
        return
    infos.append(f"yarpgen.frozen_corpus={count} informational (not a fuzz gate)")


def check_torture_fp_skip(bundlesim: Optional[Path], errs: List[str], infos: List[str]) -> None:
    if bundlesim is None:
        infos.append("gcc-torture FP skip seat skipped (BundleSim not discoverable)")
        return
    cmake = bundlesim / "bundlesim/tests/gcc_torture/CMakeLists.txt"
    if not cmake.is_file():
        errs.append("gcc-torture.cmake.missing")
        return
    text = cmake.read_text(encoding="utf-8", errors="replace")
    missing = [m for m in TORTURE_FP_MARKERS if m not in text]
    if missing:
        errs.append("gcc-torture.fp_skip.unclassified")
        return
    if "T-SF4" not in text and "softfloat" not in text:
        errs.append("gcc-torture.fp_skip.unlabeled")
        return
    infos.append("gcc-torture float/double skip classified residual (T-SF4)")


def check_library_identity(
    artifact: Optional[Path], sysroot: Optional[Path], errs: List[str], infos: List[str]
) -> None:
    """libc+libm on the matching artifact. vec_dot16 exactness stays T-DSP3."""
    if sysroot is None:
        return
    lib = sysroot / "lib"
    for name in SYSROOT_LIBS:
        if not (lib / name).is_file():
            errs.append(f"artifact.library.{name}.missing")
    if artifact is not None and artifact.is_file():
        doc = load_json(artifact)
        libblk = doc.get("library") or {}
        if not libblk:
            errs.append("artifact.library.missing")
        else:
            if _false_qualify(libblk):
                errs.append("artifact.library.false_qualified")
            if libblk.get("vec_dot16_exactness") not in (None, "residual"):
                errs.append("artifact.library.vec_dot16.false_green")
            kpi = libblk.get("kpi") or {}
            if kpi.get("m2_resweep") not in (None, "measured-miss"):
                errs.append("artifact.library.kpi.false_green")
            cov = libblk.get("coverage") or {}
            if not cov:
                errs.append("artifact.library.coverage.missing")
            else:
                if cov.get("qualified") is True or cov.get("second_matrix") is True:
                    errs.append("artifact.library.coverage.false_qualified")
                if int(cov.get("approved_expand") or 0) != APPROVED_EXPAND_COUNT:
                    errs.append("artifact.library.coverage.approved_expand")
                if int(cov.get("beyond_approved") or 0) != BEYOND_APPROVED_COUNT:
                    errs.append("artifact.library.coverage.beyond_approved")
                if int(cov.get("total_hifi3") or 0) != TOTAL_HIFI3_COUNT:
                    errs.append("artifact.library.coverage.total_hifi3")
                if cov.get("tdsp12_default_cpu") not in (None, "residual"):
                    errs.append("artifact.library.coverage.tdsp12.false_green")
                if cov.get("tdsp13_declared_vs_tested") not in (None, "residual"):
                    errs.append("artifact.library.coverage.tdsp13.false_green")
        comps = (doc.get("sysroot") or {}).get("components") or {}
        if comps and "libm.a" not in comps:
            errs.append("artifact.sysroot.libm.unstamped")
        if comps and "libc.a" not in comps:
            errs.append("artifact.sysroot.libc.unstamped")
    infos.append("library identity: libc+libm present; vec_dot16 T-DSP3 residual")


def check_kpi_measured_miss(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """M2 KPI re-sweep is measured-miss until M1 exactness + M18 records."""
    meas = llvm_src / MEASURE_SCHED
    if not meas.is_file():
        if (llvm_src / "llvm" / "utils" / "haydn").is_dir():
            errs.append("m2.kpi.measure_sched.missing")
        return
    text = meas.read_text(encoding="utf-8", errors="replace")
    if "per_op_resource_records_admitted=false" not in text:
        errs.append("m2.kpi.admission_not_failclosed")
        return
    if "Competitive II/density claims remain closed" not in text:
        errs.append("m2.kpi.competitive_unclassified")
        return
    infos.append("M2 KPI re-sweep classified measured-miss (CompleteModel=0)")


def check_em_haydn_fail_closed(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """M14: stay on EM_HAYDN=259. Do not invent a KVX replacement."""
    writer = llvm_src / ELF_WRITER
    info = llvm_src / TARGET_INFO
    if not writer.is_file():
        if (llvm_src / "llvm" / "lib" / "Target" / "Haydn" / "MCTargetDesc").is_dir():
            errs.append("em_haydn.writer.missing")
        return
    text = writer.read_text(encoding="utf-8", errors="replace")
    missing = [n for n in EM_HAYDN_WRITER_NEEDLES if n not in text]
    if missing:
        errs.append("em_haydn.writer.unpinned:" + ",".join(missing[:4]))
        return
    if "EM_KVX" in text:
        errs.append("em_haydn.writer.kvx_invent")
        return
    if info.is_file():
        info_text = info.read_text(encoding="utf-8", errors="replace")
        info_missing = [n for n in EM_HAYDN_TARGETINFO_NEEDLES if n not in info_text]
        if info_missing:
            errs.append("em_haydn.targetinfo.unpinned:" + ",".join(info_missing[:3]))
            return
    infos.append("M14 EM_HAYDN=259 fail-closed (no KVX invent)")


def check_object_profile_seats(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """e_flags + whole-parcel idle are the loader object-profile seats."""
    codegen = llvm_src / CODEGEN_EFLAGS_TEST
    if (llvm_src / "llvm" / "test" / "CodeGen" / "Haydn").is_dir():
        if not codegen.is_file():
            errs.append("object.profile.codegen_eflags.missing")
        else:
            cg = codegen.read_text(encoding="utf-8", errors="replace")
            if "EM_HAYDN=259" not in cg or "KVX" not in cg:
                errs.append("object.profile.codegen_em_haydn.kvx.unclassified")
            elif "0x103" not in cg:
                errs.append("object.profile.codegen_em.unpinned")
            else:
                infos.append(
                    "CodeGen eflags: EM_HAYDN=259 fail-closed (0x103; no KVX invent)"
                )
    lld_dir = llvm_src / "lld" / "test" / "ELF" / "haydn"
    if not lld_dir.is_dir():
        infos.append("object-profile seats skipped (no lld/test/ELF/haydn)")
        return
    missing = [rel for rel in OBJECT_PROFILE_TESTS if not (llvm_src / rel).is_file()]
    if missing:
        errs.append("object.profile.tests.missing:" + ",".join(Path(p).name for p in missing))
        return
    eflags = (llvm_src / OBJECT_PROFILE_TESTS[0]).read_text(
        encoding="utf-8", errors="replace"
    )
    if "EM_HAYDN=259" not in eflags or "KVX" not in eflags:
        errs.append("object.profile.em_haydn.kvx.unclassified")
        return
    if "0x1" not in eflags:
        errs.append("object.profile.eflags.unpinned")
        return
    idle = (llvm_src / OBJECT_PROFILE_TESTS[1]).read_text(
        encoding="utf-8", errors="replace"
    )
    if "ISA-inert zero" not in idle and "070000000000000000000000" not in idle:
        errs.append("object.profile.idle_pad.unpinned")
        return
    product_ld = (llvm_src / OBJECT_PROFILE_TESTS[2]).read_text(
        encoding="utf-8", errors="replace"
    )
    if "haydn-rt/haydn.ld" not in product_ld or "0x10000" not in product_ld:
        errs.append("object.profile.product_ld_bind.unpinned")
        return
    infos.append(
        "object-profile: EF_HAYDN_E96 + EM_HAYDN=259 fail-closed + full-slot idle"
    )


def check_m5_abi_residual(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """M5 T-ABI4/6/10/11/12 stay classified residual. Do not invent ABI."""
    identity = llvm_src / HAYDN_RT_REL / "PRODUCT-IDENTITY.txt"
    if not identity.is_file():
        if (llvm_src / "llvm" / "lib" / "Target" / "Haydn").is_dir():
            errs.append("m5.abi.identity.missing")
        return
    text = identity.read_text(encoding="utf-8", errors="replace")
    missing = [
        needle
        for needle in ("T-ABI4", "T-ABI6", "T-ABI11", "T-ABI12", "i128")
        if needle not in text
    ]
    if missing:
        errs.append("m5.abi.residual.unclassified:" + ",".join(missing))
        return
    if "product C ABI" in text and "not a" not in text.lower():
        errs.append("m5.abi.i128.false_product")
        return
    topic = Path("/ssd2/mhyang/haydn-plans/topics/runtime/TOPIC.md")
    if topic.is_file():
        topic_text = topic.read_text(encoding="utf-8", errors="replace")
        topic_missing = [
            n for n in ("T-ABI4", "T-ABI6", "T-ABI11", "T-ABI12") if n not in topic_text
        ]
        if topic_missing:
            errs.append("m5.abi.topic_stale:" + ",".join(topic_missing))
            return
    infos.append(
        "M5 T-ABI4/6/11/12 classified residual (no ISR-R0 / i128 / 'd' invent)"
    )


def check_abi_matrix_sources(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """M6 T-ABI9 monorepo compile/link sources; executed ctest stays residual."""
    utils = llvm_src / "llvm" / "utils" / "haydn"
    if not utils.is_dir():
        infos.append("ABI matrix sources skipped (not a monorepo)")
        return
    missing = [rel for rel in ABI_MATRIX_SRCS if not (llvm_src / rel).is_file()]
    if missing:
        errs.append("abi.matrix.sources.missing:" + ",".join(Path(p).name for p in missing))
        return
    callee = (llvm_src / ABI_MATRIX_SRCS[0]).read_text(encoding="utf-8", errors="replace")
    if "ret_f32" not in callee or "ret_f64" not in callee:
        errs.append("abi.matrix.softfloat_symbols.missing")
        return
    start = (llvm_src / ABI_MATRIX_SRCS[-1]).read_text(encoding="utf-8", errors="replace")
    if "_start" not in start or "consume" not in start:
        errs.append("abi.matrix.product_ld_start.missing")
        return
    runner = utils / "run_abi_conformance_matrix.sh"
    if not runner.is_file():
        errs.append("abi.matrix.runner.missing")
        return
    runner_text = runner.read_text(encoding="utf-8", errors="replace")
    if "haydn-rt/haydn.ld" not in runner_text:
        errs.append("abi.matrix.product_ld.unbound")
        return
    if "--nmagic" not in runner_text:
        errs.append("abi.matrix.nmagic.missing")
        return
    infos.append("M6 ABI compile/link matrix sources present (T-ABI9)")


def check_naturedsp_residual(tools: Optional[Path], errs: List[str], infos: List[str]) -> None:
    if tools is None:
        infos.append("NatureDSP residual census skipped (tools not discoverable)")
        return
    residual = tools / "residual-beyond-approved.txt"
    if not residual.is_file():
        errs.append("naturedsp.residual-beyond-approved.missing")
        return
    text = residual.read_text(encoding="utf-8", errors="replace")
    if "T-DSP3" not in text or "vec_dot16" not in text:
        errs.append("naturedsp.residual.vec_dot16.unclassified")
        return
    if "beyond_approved=456" not in text and "456" not in text:
        errs.append("naturedsp.residual.beyond_456.unclassified")
        return
    if "T-DSP12" not in text or "T-DSP13" not in text:
        errs.append("naturedsp.residual.tdsp12_13.unclassified")
        return
    if "do not silently" not in text.lower() and "-mcpu=haydn" not in text:
        errs.append("naturedsp.residual.tdsp12.default_cpu.unclassified")
        return
    infos.append(
        "NatureDSP residual-beyond-approved classified "
        "(T-DSP3 vec_dot16; 456 beyond; T-DSP12/13)"
    )


def check_tdsp13_inventory_pin(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """T-DSP13 declared-vs-tested is residual inventory, never QUALIFY."""
    utils = llvm_src / "llvm" / "utils" / "haydn"
    if not utils.is_dir():
        infos.append("T-DSP13 inventory skipped (not a monorepo)")
        return
    rc, report = check_tdsp13_inventory(llvm_src)
    infos.extend(str(line) for line in (report.get("info") or []))
    if rc != 0 or not report.get("ok"):
        missing = report.get("missing_snippets") or []
        extra = report.get("errors") or []
        errs.append(
            "tdsp13.declared_vs_tested.unclassified:"
            + ",".join(str(x) for x in (missing + extra)[:6])
        )
        return
    infos.append(
        "T-DSP13 declared-vs-tested residual inventory (no 746-name harness)"
    )


def check_library_torture_seats(
    llvm_src: Path, errs: List[str], infos: List[str]
) -> None:
    """user-printf + memset-2 + builtin-bitops-1 + strlen-5 + va-arg; not QUALIFY."""
    identity = llvm_src / HAYDN_RT_REL / "PRODUCT-IDENTITY.txt"
    if identity.is_file():
        text = identity.read_text(encoding="utf-8", errors="replace")
        missing = [name for name in LIBRARY_TORTURE_SEATS if name not in text]
        if missing:
            errs.append("library.torture.seats.unclassified:" + ",".join(missing))
            return
    elif (llvm_src / "llvm" / "lib" / "Target" / "Haydn").is_dir():
        errs.append("library.torture.seats.identity.missing")
        return
    execute = discover_torture_execute()
    if execute is None:
        infos.append(
            "library torture seats skipped (gcc-c-torture/execute not discoverable)"
        )
        return
    missing = [name for name in LIBRARY_TORTURE_SEATS if not (execute / name).is_file()]
    if missing:
        errs.append("library.torture.seats.missing:" + ",".join(missing))
        return
    infos.append(
        "library torture seats: user-printf.c + memset-2.c + "
        "builtin-bitops-1.c + strlen-5.c + va-arg-1.c + va-arg-2.c "
        "(classified; not QUALIFY; no torture run)"
    )


def check_libc_string_residual(
    bundlesim: Optional[Path], errs: List[str], infos: List[str]
) -> None:
    """Classify the known llvm-libc strcpy/strncmp miss; do not rebuild libc."""
    if bundlesim is None:
        infos.append("libc string residual skipped (BundleSim not discoverable)")
        return
    strcpy = bundlesim / "bundlesim/tests/campaigns/host_freestanding_strcpy.c"
    if not strcpy.is_file():
        errs.append("libc.strcpy.residual.unclassified")
        return
    text = strcpy.read_text(encoding="utf-8", errors="replace")
    if "strcpy" not in text or "miscompil" not in text.lower():
        errs.append("libc.strcpy.residual.unlabeled")
        return
    infos.append(
        "libc strcpy/strncmp classified residual (codegen; no sysroot rebuild)"
    )


def check_instruction_plugin(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """M10: parcel-only step plugin. No member decode, never QUALIFY."""
    abi = llvm_src / "lldb" / "source" / "Plugins" / "ABI" / "Haydn"
    if not abi.is_dir():
        infos.append("instruction plugin skipped (no LLDB Haydn ABI tree)")
        return
    plugin = llvm_src / INSTRUCTION_PLUGIN
    header = llvm_src / INSTRUCTION_HEADER
    if not plugin.is_file() or not header.is_file():
        errs.append("instruction.haydn.plugin.missing")
        return
    text = plugin.read_text(encoding="utf-8", errors="replace")
    missing = [n for n in INSTRUCTION_NEEDLES if n not in text]
    if missing:
        errs.append("instruction.haydn.plugin.incomplete:" + ",".join(missing))
        return
    if "GetOpcodeForInstruction" in text:
        errs.append("instruction.haydn.member_decode_invent")
        return
    cmake = llvm_src / "lldb" / "source" / "Plugins" / "Instruction" / "CMakeLists.txt"
    if cmake.is_file():
        cmake_text = cmake.read_text(encoding="utf-8", errors="replace")
        if "Haydn" not in cmake_text:
            errs.append("instruction.haydn.cmake.unwired")
            return
    infos.append("M10 Instruction plugin: parcel12 step; no member decode")


def check_clang_nmagic(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    """Matching-sysroot --nmagic + -lm. Product ld is not auto-injected."""
    driver = llvm_src / CLANG_HAYDN_DRIVER
    if not driver.is_file():
        infos.append("clang Haydn driver skipped (not in tree)")
        return
    text = driver.read_text(encoding="utf-8", errors="replace")
    missing = [n for n in CLANG_HAYDN_NEEDLES if n not in text]
    if missing:
        errs.append("clang.haydn.link_args.missing:" + ",".join(missing))
        return
    if "haydn.ld" in text and "Do not auto-inject haydn.ld" not in text:
        errs.append("clang.haydn.ld.auto_inject")
        return
    infos.append("clang Haydn link: --nmagic + matching-sysroot -lm")


def check_owned_helpers(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    utils = llvm_src / "llvm" / "utils" / "haydn"
    if not utils.is_dir():
        infos.append("owned helpers skipped (not a monorepo)")
        return
    missing = [rel for rel in OWNED_HELPERS if not (llvm_src / rel).is_file()]
    if missing:
        errs.append("owned.helpers.missing:" + ",".join(Path(p).name for p in missing[:6]))
        return
    infos.append("owned helpers present (verdict/step/abi/ledger)")


def check_freestanding_shims(llvm_src: Path, errs: List[str], infos: List[str]) -> None:
    root = llvm_src / "llvm" / "utils" / "haydn" / "freestanding-shims"
    if not root.is_dir():
        infos.append("freestanding shims skipped (not a monorepo)")
        return
    missing = [rel for rel in FREESTANDING_SHIMS if not (root / rel).is_file()]
    if missing:
        errs.append("freestanding.shims.missing:" + ",".join(missing[:6]))
        return
    infos.append(f"freestanding shims present ({len(FREESTANDING_SHIMS)})")


def check_freeze_notes(topic_paths: List[Path], errs: List[str], infos: List[str]) -> None:
    present = [p for p in topic_paths if p.is_file()]
    if not present:
        infos.append("freeze note skipped (topics not discoverable)")
        return
    ok = 0
    for path in present:
        text = path.read_text(encoding="utf-8", errors="replace")
        if FREEZE_NEEDLE in text and all(item in text for item in FREEZE_IDS):
            ok += 1
        else:
            errs.append(f"freeze.missing:{path.name}")
    if ok:
        infos.append(f"M16/M19-M22 freeze noted in {ok} topic(s) until SF1-SF3")


def check(
    *,
    llvm_src: Path,
    sysroot: Optional[Path],
    bundlesim: Optional[Path],
    require_sysroot: bool,
    haydn_bin: Optional[Path] = None,
    require_consumer_install: bool = False,
) -> Tuple[int, Dict[str, Any]]:
    errs: List[str] = []
    infos: List[str] = []
    if sysroot is None:
        if require_sysroot:
            errs.append("sysroot.missing")
        else:
            infos.append("sysroot absent; artifact/hygiene seats skipped")
    else:
        artifact = sysroot / "ARTIFACT.json"
        if not artifact.is_file():
            errs.append("ARTIFACT.json.missing")
            check_library_identity(None, sysroot, errs, infos)
        else:
            check_debug_step_contract(artifact, errs)
            check_decode_contract(artifact, errs)
            check_decode_bind(artifact, errs, infos)
            check_consumer_contract(artifact, errs)
            check_no_semantic_qualify(artifact, errs)
            check_toolchain_bind(artifact, haydn_bin, bundlesim, errs, infos)
            check_golden_inputs_pin_not_gate(artifact, infos)
            check_library_identity(artifact, sysroot, errs, infos)
            check_product_ld_identity(artifact, llvm_src, errs, infos)
            check_artifact_rebind(artifact, errs, infos, llvm_src)
        check_sysroot_hygiene(sysroot, errs)
        check_fp_in_gate(sysroot, haydn_bin, errs, infos)
    check_haydn_rt_contracts(llvm_src, errs, infos)
    check_owned_helpers(llvm_src, errs, infos)
    check_instruction_plugin(llvm_src, errs, infos)
    check_clang_nmagic(llvm_src, errs, infos)
    check_freestanding_shims(llvm_src, errs, infos)
    check_builtins_overlay(llvm_src, errs, infos)
    check_consumer_c_residual(llvm_src, bundlesim, errs, infos)
    check_ieee_residual(llvm_src, errs, infos)
    check_tsf9_contract(llvm_src, errs, infos)
    check_yarpgen_seat(bundlesim, errs, infos)
    check_torture_fp_skip(bundlesim, errs, infos)
    check_naturedsp_residual(discover_naturedsp_tools(llvm_src), errs, infos)
    check_tdsp13_inventory_pin(llvm_src, errs, infos)
    check_kpi_measured_miss(llvm_src, errs, infos)
    check_object_profile_seats(llvm_src, errs, infos)
    check_em_haydn_fail_closed(llvm_src, errs, infos)
    check_abi_matrix_sources(llvm_src, errs, infos)
    check_m5_abi_residual(llvm_src, errs, infos)
    check_product_ld_install_path(llvm_src, errs, infos)
    check_consumer_install_script(
        bundlesim, errs, infos, require=require_consumer_install
    )
    check_library_torture_seats(llvm_src, errs, infos)
    check_torture_image_reject(llvm_src, bundlesim, errs, infos)
    check_lld_control_target(llvm_src, errs, infos)
    check_libc_string_residual(bundlesim, errs, infos)
    plans = Path("/ssd2/mhyang/haydn-plans/topics")
    check_freeze_notes(
        [
            plans / "runtime/TOPIC.md",
            plans / "bundlesim/TOPIC.md",
            plans / "softfloat/TOPIC.md",
        ],
        errs,
        infos,
    )
    report = {
        "ok": not errs,
        "errors": errs,
        "info": infos,
        "sysroot": str(sysroot) if sysroot else None,
        "bundlesim": str(bundlesim) if bundlesim else None,
    }
    return (0 if not errs else 1), report


def _stamp_decode_fixture(bundlesim: Path) -> Dict[str, Dict[str, Dict[str, str]]]:
    names = (
        ("Slot0", "slot0_alu.h"),
        ("Slot0", "slot0_ls.h"),
        ("Slot1", "slot1_alu.h"),
        ("Slot1", "slot1_load.h"),
        ("Slot1", "slot1_mac.h"),
        ("Slot2", "slot2_alu.h"),
        ("Slot2", "slot2_mac.h"),
    )
    shims: Dict[str, Dict[str, str]] = {}
    models: Dict[str, Dict[str, str]] = {}
    for slot, name in names:
        shim_p = bundlesim / "bundlesim/isa/Functional_Model" / slot / name
        model_p = bundlesim / "bundlesim/isa/model" / slot / name
        shim_p.parent.mkdir(parents=True, exist_ok=True)
        model_p.parent.mkdir(parents=True, exist_ok=True)
        shim_p.write_text(f'#include "../model/{name}"\n', encoding="utf-8")
        model_p.write_text(f"// model {name}\n", encoding="utf-8")
        shims[name] = {"path": str(shim_p), "sha256": file_sha256(shim_p)}
        models[name] = {"path": str(model_p), "sha256": file_sha256(model_p)}
    return {"shims": shims, "model": models}


def _self_test() -> int:
    assert "__addsf3" in parse_nm_defined("00000000 T __addsf3\n         U other\n")
    assert "__adddf3" in parse_nm_defined("T __adddf3\n")
    assert "__addsf3" not in parse_nm_defined("         U __addsf3\n")
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        sysroot = root / "sysroot"
        lib = sysroot / "lib"
        lib.mkdir(parents=True)
        (lib / "libc.a").write_bytes(b"libc")
        (lib / "libm.a").write_bytes(b"libm")
        (sysroot / "ARTIFACT.json").write_text(
            json.dumps(
                {
                    "debug": {
                        "complete": True,
                        "parcel_bytes": 12,
                        "step_inst": {
                            "preferred_class": "parcel12",
                            "same_pc_class": "residual",
                            "non12_delta_class": "residual",
                            "qualified": False,
                        },
                        "eflags": {
                            "external_allocation": False,
                            "policy": "provisional-consumer-agreement",
                            "value": 1,
                        },
                        "register_namespaces": {"do_not_equate": True},
                    },
                    "decode": {
                        "authority": "five-file",
                        "complete": True,
                        "shim_discipline_ok": True,
                        "shim_count": 7,
                        "model_count": 7,
                    },
                    "consumer": {
                        "complete": True,
                        "debugger": {"role": "lldb", "sha256": "ab" * 32},
                        "loader": {"role": "bundlesim", "sha256": "cd" * 32},
                    },
                    "toolchain": {
                        "clang": {"sha256": "11" * 32},
                        "llc": {"sha256": "22" * 32},
                        "ld.lld": {"sha256": "33" * 32},
                        "lldb": {"sha256": "ab" * 32},
                    },
                    "product_ld": {
                        "authority": "haydn-rt/haydn.ld",
                        "complete": True,
                        "body_match": True,
                    },
                    "sysroot": {
                        "components": {
                            "libc.a": {"sha256": "aa" * 32},
                            "libm.a": {"sha256": "bb" * 32},
                        }
                    },
                    "library": {
                        "authority": "product_library_pin",
                        "complete": True,
                        "qualified": False,
                        "vec_dot16_exactness": "residual",
                        "kpi": {"m2_resweep": "measured-miss"},
                        "coverage": {
                            "approved_expand": 35,
                            "beyond_approved": 456,
                            "total_hifi3": 491,
                            "authority": "product_library_pin",
                            "qualified": False,
                            "second_matrix": False,
                            "tdsp12_default_cpu": "residual",
                            "tdsp13_declared_vs_tested": "residual",
                        },
                    },
                    "semantic_qualify": False,
                    "llvm_src": {
                        "git_commit": "4b6677f8bf87d12f20e4d0b0ff5ec10124dddf2f"
                    },
                }
            ),
            encoding="utf-8",
        )
        bundlesim = root / "bs"
        cases = bundlesim / "bundlesim/tests/regression/cases"
        cases.mkdir(parents=True)
        (cases / "YARPGEN_MANIFEST.json").write_text(
            json.dumps(
                {
                    "count": 28,
                    "cases": [{"name": f"yarpgen_seed{i}"} for i in range(28)],
                }
            ),
            encoding="utf-8",
        )
        campaigns = bundlesim / "bundlesim/tests/campaigns"
        campaigns.mkdir(parents=True)
        (campaigns / "host_freestanding_strcpy.c").write_text(
            "/* Haydn llvm-libc strcpy path that has miscompiled under current codegen. */\n"
            "char *strcpy(char *d, const char *s);\n",
            encoding="utf-8",
        )
        (campaigns / "run_coremark_qualification.py").write_text(
            'fail("TARGET_BUILD_FAILED", "Haydn compile")\n',
            encoding="utf-8",
        )
        (campaigns / "run_dhrystone_qualification.py").write_text(
            'fail("TARGET_BUILD_FAILED", "Haydn compile")\n',
            encoding="utf-8",
        )
        decode_files = _stamp_decode_fixture(bundlesim)
        art_path = sysroot / "ARTIFACT.json"
        art_doc = json.loads(art_path.read_text(encoding="utf-8"))
        art_doc["decode"].update(decode_files)
        art_path.write_text(json.dumps(art_doc), encoding="utf-8")
        runtime = root / HAYDN_RT_REL
        runtime.mkdir(parents=True)
        (runtime / "SOFTFLOAT-CONTRACT.txt").write_text(
            "__addsf3 __adddf3 long double _Float16 unavailable __SOFTFP__ "
            "T-SF10 yarpgen .bak .broken 28700d57\n",
            encoding="utf-8",
        )
        (runtime / "NATUREDSP-CANARY-PIN.txt").write_text(
            "vec_scale32x32_fast_hifi3 vec_shift32x32_fast_hifi3 product_library_pin 456 "
            "T-DSP13 declared-vs-tested inventory only; no 746-name harness\n",
            encoding="utf-8",
        )
        (runtime / "PRODUCT-IDENTITY.txt").write_text(
            "ARTIFACT.json parcel12 EM_HAYDN=259 decode live bind; do not invent a fuzz gate "
            "T-DSP13 declared-vs-tested inventory only; no 746-name harness "
            "install_product_ld ARTIFACT.product_ld .bak .broken "
            "user-printf.c memset-2.c builtin-bitops-1.c strlen-5.c "
            "va-arg-1.c va-arg-2.c CODE_IMAGE_REJECT "
            "direct control target is not an exact code record "
            "4b6677f8bf87 28700d57 T-ABI4 T-ABI6 T-ABI11 T-ABI12 i128 "
            "G-ECOSYSTEM-CONSUMERS G-LIBRARY-COVERAGE "
            "G-DEBUG-OBSERVABILITY G-TEST-EVIDENCE "
            "not a product C ABI\n",
            encoding="utf-8",
        )
        anchors = root / AUTHORITY_ANCHORS
        anchors.parent.mkdir(parents=True)
        anchors.write_text(
            "consumer C CoreMark/Dhrystone TARGET_BUILD_FAILED residual\n"
            "T-SF5 IEEE vector conformance residual; do not invent TestFloat vectors\n"
            "T-SF9 contract __addsf3 __adddf3 long double _Float16 T-SF10\n"
            "T-DSP13 declared-vs-tested inventory only; no 746-name harness\n",
            encoding="utf-8",
        )
        (anchors.parent / "FAULT-INJECTION-SEATS.txt").write_text(
            "T-DSP13 declared-vs-tested inventory only; no 746-name harness\n",
            encoding="utf-8",
        )
        (anchors.parent / "CORRUPTION-MATRIX.txt").write_text(
            "T-DSP13 declared-vs-tested; 746 harness forbidden\n",
            encoding="utf-8",
        )
        libcall = root / SOFTFLOAT_LIBCALL_PIN
        libcall.parent.mkdir(parents=True, exist_ok=True)
        libcall.write_text("; CHECK: jal lr, __addsf3\n", encoding="utf-8")
        (root / CODEGEN_EFLAGS_TEST).write_text(
            "; EM_HAYDN=259 Machine: 0x103 Kalray KVX fail-closed\n",
            encoding="utf-8",
        )
        product = root / "haydn-rt" / "haydn.ld"
        product.parent.mkdir(parents=True, exist_ok=True)
        product.write_text("OUTPUT_ARCH(haydn)\n", encoding="utf-8")
        cmake = bundlesim / "bundlesim/tests/gcc_torture"
        cmake.mkdir(parents=True)
        (cmake / "CMakeLists.txt").write_text(
            "\n".join(
                [
                    "# Haydn FP: keep skipped here (unbounded gcc-torture float/double).",
                    "# T-SF4 closed f32/f64 value matrix is ctest -L softfloat",
                    '"[^a-zA-Z0-9_]float[^a-zA-Z0-9_]"',
                    '"[^a-zA-Z0-9_]double[^a-zA-Z0-9_]"',
                ]
            ),
            encoding="utf-8",
        )
        fe = bundlesim / "bundlesim/tests/unit"
        fe.mkdir(parents=True, exist_ok=True)
        (fe / "test_frontend_pipeline.c").write_text(
            "CHECK(status.code == BS_CODE_IMAGE_REJECT);\n",
            encoding="utf-8",
        )
        tools = root / "tools"
        tools.mkdir()
        (tools / "residual-beyond-approved.txt").write_text(
            "T-DSP3 vec_dot16 exactness residual\n"
            "CENSUS: beyond_approved=456 approved_expand=35 total_hifi3=491\n"
            "T-DSP12 do not silently default -mcpu=haydn\n"
            "T-DSP13 declared-vs-tested residual\n",
            encoding="utf-8",
        )
        topics = root / "topics"
        topics.mkdir()
        freeze = (
            "Freeze M16/M19-M22 until SF1-SF3. Do not revive M16 M19 M20 M21 M22.\n"
        )
        for name in ("runtime.md", "bundlesim.md", "softfloat.md"):
            (topics / name).write_text(freeze, encoding="utf-8")

        rec = root / "llvm" / "utils" / "haydn" / "record_haydn_artifact_set.py"
        rec.parent.mkdir(parents=True, exist_ok=True)
        rec.write_text("no bind\n", encoding="utf-8")
        e, i = [], []
        check_library_torture_seats(root, e, i)
        assert not e, e
        assert any("user-printf.c" in x for x in i) or any(
            "skipped" in x for x in i
        ), i
        e, i = [], []
        check_torture_image_reject(root, bundlesim, e, i)
        assert not e, e
        assert any("CODE_IMAGE_REJECT" in x for x in i), i
        assert any(IMAGE_REJECT_MSG in x for x in i), i
        identity = root / HAYDN_RT_REL / "PRODUCT-IDENTITY.txt"
        old_ident = identity.read_text(encoding="utf-8")
        identity.write_text("ARTIFACT.json no torture seats\n", encoding="utf-8")
        e, i = [], []
        check_library_torture_seats(root, e, i)
        assert any("unclassified" in x for x in e), e
        e, i = [], []
        check_torture_image_reject(root, bundlesim, e, i)
        assert any("image_loader.reject.unclassified" in x for x in e), e
        identity.write_text("CODE_IMAGE_REJECT no exact record msg\n", encoding="utf-8")
        e, i = [], []
        check_torture_image_reject(root, bundlesim, e, i)
        assert any("image_loader.reject.msg.unclassified" in x for x in e), e
        identity.write_text(old_ident, encoding="utf-8")
        e, i = [], []
        check_m5_abi_residual(root, e, i)
        assert not e, e
        assert any("T-ABI4" in x for x in i), i
        identity.write_text("ARTIFACT.json no abi residuals\n", encoding="utf-8")
        e, i = [], []
        check_m5_abi_residual(root, e, i)
        assert any("m5.abi.residual.unclassified" in x for x in e), e
        identity.write_text(old_ident, encoding="utf-8")
        lld = root / LLD_HAYDN
        lld.parent.mkdir(parents=True, exist_ok=True)
        lld.write_text("no pin\n", encoding="utf-8")
        e, i = [], []
        check_lld_control_target(root, e, i)
        assert any("control_target.unclassified" in x for x in e), e
        lld.write_text(
            "trapInstr = {0x00, 0x00, 0x00, 0x00}\n"
            "CODE_IMAGE_REJECT\n"
            "direct control target is not an exact code record\n"
            "nopFiller\n",
            encoding="utf-8",
        )
        e, i = [], []
        check_lld_control_target(root, e, i)
        assert not e, e
        assert any("trap-fill" in x for x in i), i
        writer = root / ELF_WRITER
        writer.parent.mkdir(parents=True, exist_ok=True)
        writer.write_text("no pin\n", encoding="utf-8")
        e, i = [], []
        check_em_haydn_fail_closed(root, e, i)
        assert any("em_haydn.writer.unpinned" in x for x in e), e
        writer.write_text(
            "static_assert(ELF::EM_HAYDN == 259, \"stay\");\n"
            "// Official 259 is Kalray KVX. do not invent a replacement.\n",
            encoding="utf-8",
        )
        info = root / TARGET_INFO
        info.parent.mkdir(parents=True, exist_ok=True)
        info.write_text(
            "// EM_HAYDN=259 collides with official Kalray KVX.\n"
            "// Do not invent a replacement here.\n",
            encoding="utf-8",
        )
        e, i = [], []
        check_em_haydn_fail_closed(root, e, i)
        assert not e, e
        assert any("EM_HAYDN=259" in x for x in i), i
        writer.write_text(writer.read_text(encoding="utf-8") + "EM_KVX\n", encoding="utf-8")
        e, i = [], []
        check_em_haydn_fail_closed(root, e, i)
        assert any("kvx_invent" in x for x in e), e
        writer.write_text(
            "static_assert(ELF::EM_HAYDN == 259, \"stay\");\n"
            "// Official 259 is Kalray KVX. do not invent a replacement.\n",
            encoding="utf-8",
        )
        e, i = [], []
        check_product_ld_install_path(root, e, i)
        assert any("install_path.missing" in x for x in e), e
        rec.write_text(
            "def install_product_ld():\n    # haydn-rt/haydn.ld\n    # refuse .bak and .broken\n",
            encoding="utf-8",
        )
        e, i = [], []
        check_product_ld_install_path(root, e, i)
        assert not e, e
        rec.unlink()
        try:
            rec.parent.rmdir()
            rec.parent.parent.rmdir()
            rec.parent.parent.parent.rmdir()
        except OSError:
            pass

        inst = bundlesim / "scripts" / "install_haydn_sysroot.sh"
        inst.parent.mkdir(parents=True, exist_ok=True)
        inst.write_text("echo bsp only\n", encoding="utf-8")
        e, i = [], []
        check_consumer_install_script(bundlesim, e, i, require=True)
        assert any("product_ld" in x or "hygiene" in x for x in e), e
        e, i = [], []
        check_consumer_install_script(bundlesim, e, i, require=False)
        assert not e, e
        assert any("T7-RT" in x or "product_ld" in x for x in i), i
        inst.write_text(
            "# bind _product_ld from haydn-rt/haydn.ld\n"
            "_product_ld=...\n"
            "# refuse .bak / .broken debris\n"
            "_self_test_product_ld() { echo product-ld self-test OK; }\n"
            'if [[ "${1:-}" == "--self-test" ]]; then _self_test_product_ld; fi\n',
            encoding="utf-8",
        )
        e, i = [], []
        check_consumer_install_script(bundlesim, e, i, require=True)
        assert not e, e
        inst.write_text("echo bsp only\n", encoding="utf-8")
        rc_req, rep_req = check(
            llvm_src=root,
            sysroot=sysroot,
            bundlesim=bundlesim,
            require_sysroot=True,
            require_consumer_install=True,
        )
        assert rc_req == 1 and any(
            "product_ld" in x or "hygiene" in x or "install" in x
            for x in rep_req["errors"]
        ), rep_req
        inst.write_text(
            "# bind _product_ld from haydn-rt/haydn.ld\n"
            "_product_ld=...\n"
            "# refuse .bak / .broken debris\n"
            "_self_test_product_ld() { echo product-ld self-test OK; }\n"
            'if [[ "${1:-}" == "--self-test" ]]; then _self_test_product_ld; fi\n',
            encoding="utf-8",
        )

        old_tools = os.environ.get("TOOLS")
        os.environ["TOOLS"] = str(tools)
        try:
            # Hygiene green.
            rc, rep = check(
                llvm_src=root,
                sysroot=sysroot,
                bundlesim=bundlesim,
                require_sysroot=True,
            )
            # Freeze notes live outside this temp tree; ignore those errors here.
            filtered = [e for e in rep["errors"] if not e.startswith("freeze.missing")]
            assert rc == 1 or not filtered
            assert "sysroot.hygiene" not in ",".join(filtered), rep

            # False-qualified step must fail.
            doc = json.loads((sysroot / "ARTIFACT.json").read_text(encoding="utf-8"))
            doc["debug"]["step_inst"]["qualified"] = True
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")
            rc, rep = check(
                llvm_src=root,
                sysroot=sysroot,
                bundlesim=bundlesim,
                require_sysroot=True,
            )
            assert rc == 1 and any("false_qualified" in e for e in rep["errors"]), rep

            doc["debug"]["step_inst"]["qualified"] = False
            # Compile-only QUALIFY label must fail.
            doc["semantic_qualify"] = True
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")
            rc, rep = check(
                llvm_src=root,
                sysroot=sysroot,
                bundlesim=bundlesim,
                require_sysroot=True,
            )
            assert rc == 1 and any("semantic_qualify" in e for e in rep["errors"]), rep
            doc["semantic_qualify"] = False

            # Incomplete decode identity must fail.
            dec_ok = dict(doc["decode"])
            doc["decode"]["complete"] = False
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")
            rc, rep = check(
                llvm_src=root,
                sysroot=sysroot,
                bundlesim=bundlesim,
                require_sysroot=True,
            )
            assert rc == 1 and any("decode.incomplete" in e for e in rep["errors"]), rep
            doc["decode"] = dec_ok
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")

            # Live Functional_Model/model hash drift must unbind.
            model_meta = next(iter((doc["decode"].get("model") or {}).values()))
            model_path = Path(model_meta["path"])
            original_model = model_path.read_text(encoding="utf-8")
            model_path.write_text(original_model + "// drift\n", encoding="utf-8")
            rc, rep = check(
                llvm_src=root,
                sysroot=sysroot,
                bundlesim=bundlesim,
                require_sysroot=True,
            )
            model_path.write_text(original_model, encoding="utf-8")
            # Hash drift is informational on this tree (not a compiler fail).
            assert any(
                ".unbound" in e and "decode." in e
                for e in (rep["errors"] + rep.get("info") or [])
            ), rep

            # Library QUALIFY label must fail.
            lib_ok = dict(doc["library"])
            doc["library"]["qualified"] = True
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")
            rc, rep = check(
                llvm_src=root,
                sysroot=sysroot,
                bundlesim=bundlesim,
                require_sysroot=True,
            )
            assert rc == 1 and any("library.false_qualified" in e for e in rep["errors"]), rep
            doc["library"] = lib_ok
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")

            # Missing library stamp is an unbound same-artifact identity.
            dropped = dict(doc)
            dropped.pop("library", None)
            (sysroot / "ARTIFACT.json").write_text(json.dumps(dropped), encoding="utf-8")
            rc, rep = check(
                llvm_src=root,
                sysroot=sysroot,
                bundlesim=bundlesim,
                require_sysroot=True,
            )
            assert rc == 1 and any("library.missing" in e for e in rep["errors"]), rep
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")

            # Missing 456-beyond coverage is an unbound library identity.
            dropped_cov = dict(doc)
            dropped_cov["library"] = dict(doc["library"])
            dropped_cov["library"].pop("coverage", None)
            (sysroot / "ARTIFACT.json").write_text(json.dumps(dropped_cov), encoding="utf-8")
            rc, rep = check(
                llvm_src=root,
                sysroot=sysroot,
                bundlesim=bundlesim,
                require_sysroot=True,
            )
            assert rc == 1 and any("coverage.missing" in e for e in rep["errors"]), rep
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")

            # Stale 28700d57 full-gate commit is fail-closed (rebind 4b6677f8bf87).
            llvm_ok = dict(doc.get("llvm_src") or {})
            doc["llvm_src"] = {
                "git_commit": "28700d57366a35a7d04e8adfbdf782743ec847e0"
            }
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")
            rc, rep = check(
                llvm_src=root,
                sysroot=sysroot,
                bundlesim=bundlesim,
                require_sysroot=True,
            )
            assert rc == 1 and any("28700d57" in e for e in rep["errors"]), rep
            doc["llvm_src"] = llvm_ok
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")

            # Live monorepo: 4b6677f8bf87 is an ancestor; a missing object is not.
            live_repo = Path("/ssd/mhyang/llvm/llvm-head")
            if _git_work_tree(live_repo):
                assert git_commit_is_repo_resident(live_repo, REBIND_ANCESTOR), (
                    live_repo,
                    REBIND_ANCESTOR,
                )
                assert not git_commit_is_repo_resident(
                    live_repo, "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
                )
                assert not git_commit_is_repo_resident(
                    live_repo, "28700d57366a35a7d04e8adfbdf782743ec847e0"
                )

            # Live clang hash drift after rebuild must unbind (no ARTIFACT rewrite).
            fake_bin = root / "bin"
            fake_bin.mkdir()
            clang_path = fake_bin / "clang"
            clang_path.write_bytes(b"clang-live")
            stamped_clang = "00" * 32
            doc["toolchain"]["clang"] = {
                "path": str(clang_path),
                "sha256": stamped_clang,
            }
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")
            rc, rep = check(
                llvm_src=root,
                sysroot=sysroot,
                bundlesim=bundlesim,
                require_sysroot=True,
                haydn_bin=fake_bin,
            )
            assert rc == 1 and any("clang.unbound" in e for e in rep["errors"]), rep
            leftover = json.loads(
                (sysroot / "ARTIFACT.json").read_text(encoding="utf-8")
            )
            assert leftover["toolchain"]["clang"]["sha256"] == stamped_clang
            assert leftover["toolchain"]["clang"]["sha256"] != file_sha256(
                clang_path
            )
            doc["toolchain"]["clang"] = {"sha256": "11" * 32}
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")

            # GOLDEN_INPUTS pin count is inventory, not a product-gate fail.
            doc["catalog"] = {
                "authority": "five-file",
                "complete": True,
                "golden_inputs_pin": {
                    "file_count": 9,
                    "five_file_complete": True,
                },
            }
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")
            rc, rep = check(
                llvm_src=root,
                sysroot=sysroot,
                bundlesim=bundlesim,
                require_sysroot=True,
            )
            assert rc == 0, rep
            assert any("GOLDEN_INPUTS pin file_count=9" in e for e in rep["info"]), rep
            assert not any("GOLDEN_INPUTS" in e for e in rep["errors"]), rep
            doc.pop("catalog", None)
            (sysroot / "ARTIFACT.json").write_text(json.dumps(doc), encoding="utf-8")

            # Debris fail-closed.
            (lib / "libc.a.broken-t5").write_text("x", encoding="utf-8")
            rc, rep = check(
                llvm_src=root,
                sysroot=sysroot,
                bundlesim=bundlesim,
                require_sysroot=True,
            )
            assert rc == 1 and any("hygiene" in e for e in rep["errors"]), rep
        finally:
            if old_tools is None:
                os.environ.pop("TOOLS", None)
            else:
                os.environ["TOOLS"] = old_tools

    print("check_runtime_artifact_seats self-test OK")
    return 0


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--llvm-src", type=Path, default=None)
    ap.add_argument("--sysroot", type=Path, default=None)
    ap.add_argument("--bundlesim", type=Path, default=None)
    ap.add_argument("--haydn-bin", type=Path, default=None)
    ap.add_argument("--require-sysroot", action="store_true")
    ap.add_argument(
        "--require-consumer-install",
        action="store_true",
        help="fail when the BundleSim working-tree install is unbound "
        "(committed HEAD stays residual OPEN)",
    )
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    if args.self_test:
        return _self_test()

    llvm_src = (args.llvm_src or monorepo_from_script()).resolve()
    haydn_bin = args.haydn_bin
    if haydn_bin is None and os.environ.get("HAYDN_BIN"):
        haydn_bin = Path(os.environ["HAYDN_BIN"])
    sysroot = discover_sysroot(args.sysroot, haydn_bin)
    bundlesim = discover_bundlesim(args.bundlesim)
    rc, report = check(
        llvm_src=llvm_src,
        sysroot=sysroot,
        bundlesim=bundlesim,
        require_sysroot=args.require_sysroot,
        haydn_bin=haydn_bin,
        require_consumer_install=args.require_consumer_install,
    )
    if args.json:
        json.dump(report, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        for line in report["info"]:
            print(f"  INFO: {line}")
        if report["ok"]:
            print("check_runtime_artifact_seats: PASS")
        else:
            for err in report["errors"]:
                print(f"  FAIL: {err}", file=sys.stderr)
            print("check_runtime_artifact_seats: FAIL", file=sys.stderr)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
