#!/usr/bin/env python3
"""Monorepo recorder for one Clang→sysroot→consumer Haydn artifact.

G-RUNTIME-TOOLCHAIN: `$sysroot/ARTIFACT.json` is the product identity.
This script is the in-tree seat. Full toolchain/sysroot/golden hashing
delegates to BundleSim `scripts/record_haydn_artifact_set.py` when that
sibling is present. This file always owns:

  * haydn-rt/haydn.ld as the product linker script (T-MC10 / M12)
  * installed `$sysroot/lib/{haydn.ld,bundlesim.ld}` body must match that product
  * install_product_ld is the in-tree install path (not BSP-only)
  * .bak / .broken* debris is refused at install
  * libc.a + libm.a library identity (product_library_pin; vec_dot16 residual)
  * `--restamp` refreshes identity after a toolchain rebuild
  * `--install-product-ld` / `--attach-owned` bind haydn.ld and stamp
    product_ld+library onto an existing ARTIFACT.json
  * consumer install_haydn_sysroot.sh bind is landed (BundleSim edbb813).
    --require-consumer-install fail-closes on the working-tree script,
    the committed checkout identity, and --self-test.
  * live ARTIFACT.product_ld null is fail-closed when ARTIFACT.json exists
  * post-wave rebind refuses llvm_src.git_commit=28700d57; the live
    commit must be a repo-object ancestor (pin 4b6677f8bf87)

Usage:
  record_haydn_artifact_set.py --self-test
  record_haydn_artifact_set.py --check-product-ld [--llvm-src PATH] [--sysroot PATH]
  record_haydn_artifact_set.py --check-library [--llvm-src PATH] [--sysroot PATH]
  record_haydn_artifact_set.py --install-product-ld [--llvm-src PATH] [--sysroot PATH]
  record_haydn_artifact_set.py --attach-owned [--llvm-src PATH] [--sysroot PATH]
  record_haydn_artifact_set.py [--restamp] [--out PATH]   # delegate + product_ld

Exit:
  0 success
  1 pin / delegate failure
  2 usage
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Dict, List, Optional

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

import check_product_ld as product_ld  # noqa: E402

PRODUCT_REL = Path("haydn-rt/haydn.ld")
SYSROOT_LD_NAMES = ("bundlesim.ld", "haydn.ld")
SYSROOT_LIBS = ("libc.a", "libm.a")
INSTALL_REL = Path("scripts/install_haydn_sysroot.sh")
HYGIENE_SUFFIXES = (".bak", ".broken", ".broken-t5")
HYGIENE_NAME_PARTS = (".bak", ".broken")
STALE_FULL_GATE_COMMIT = "28700d57"
REBIND_ANCESTOR = "4b6677f8bf87"
NATUREDSP_PIN = Path("/ssd2/mhyang/haydn-plans/naturedsp-haydn/tools/product_library_pin.sh")
NATUREDSP_RESIDUAL = Path(
    "/ssd2/mhyang/haydn-plans/naturedsp-haydn/tools/residual-beyond-approved.txt"
)
NATUREDSP_SRC = Path("/ssd2/mhyang/haydn-plans/hifi_naturedsp_reports/src/library")
APPROVED_EXPAND_COUNT = 35
BEYOND_APPROVED_COUNT = 456
TOTAL_HIFI3_COUNT = 491
_RE_APPROVED_KERNEL = re.compile(r"^\s+([a-z0-9_]+_hifi3)$", re.M)


def monorepo_from_script() -> Path:
    return Path(__file__).resolve().parents[3]


def census_naturedsp_hifi3(
    src: Optional[Path] = None, pin: Optional[Path] = None
) -> Optional[Dict[str, Any]]:
    """Closed 35-name expand list vs live *_hifi3.c. Not a second matrix."""
    pin_path = pin if pin is not None else (
        NATUREDSP_PIN if NATUREDSP_PIN.is_file() else None
    )
    src_path = src if src is not None else (
        NATUREDSP_SRC if NATUREDSP_SRC.is_dir() else None
    )
    if pin_path is None or src_path is None or not src_path.is_dir():
        return None
    names = _RE_APPROVED_KERNEL.findall(
        pin_path.read_text(encoding="utf-8", errors="replace")
    )
    approved = len(names)
    total = len({p.stem for p in src_path.rglob("*_hifi3.c")})
    return {
        "approved_expand": approved,
        "beyond_approved": total - approved,
        "total_hifi3": total,
        "authority": "product_library_pin",
        "qualified": False,
        "second_matrix": False,
        "tdsp12_default_cpu": "residual",
        "tdsp13_declared_vs_tested": "residual",
    }


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_llvm_src(explicit: Optional[Path], haydn_bin: Optional[Path]) -> Path:
    candidates: List[Path] = []
    if explicit:
        candidates.append(explicit)
    for key in ("HAYDN_LLVM_SRC", "LLVM_SRC", "LLVM_PROJECT"):
        val = os.environ.get(key)
        if val:
            candidates.append(Path(val))
    candidates.append(monorepo_from_script())
    if haydn_bin is not None:
        cache = haydn_bin.parent / "CMakeCache.txt"
        if cache.is_file():
            try:
                for line in cache.read_text(
                    encoding="utf-8", errors="replace"
                ).splitlines():
                    if line.startswith("LLVM_SOURCE_DIR:STATIC="):
                        val = line.split("=", 1)[1].strip()
                        if val:
                            candidates.append(Path(val))
            except OSError:
                pass
    candidates.append(Path("/ssd/mhyang/llvm/llvm-head"))
    for cand in candidates:
        try:
            root = cand.resolve()
        except OSError:
            continue
        if (root / PRODUCT_REL).is_file():
            return root
    raise FileNotFoundError("haydn-rt/haydn.ld not found (set LLVM_SRC)")


def resolve_bundlesim(explicit: Optional[Path]) -> Optional[Path]:
    candidates: List[Path] = []
    if explicit:
        candidates.append(explicit)
    for key in ("BUNDLESIM_ROOT", "BUNDLE_REPO", "BUNDLESIM"):
        val = os.environ.get(key)
        if val:
            candidates.append(Path(val))
    candidates.append(Path("/ssd2/mhyang/BundleSim"))
    for cand in candidates:
        try:
            root = cand.resolve()
        except OSError:
            continue
        if (root / "scripts" / "install_haydn_sysroot.sh").is_file():
            return root
    return None


def resolve_sysroot(explicit: Optional[Path], haydn_bin: Optional[Path]) -> Optional[Path]:
    return product_ld.discover_sysroot(explicit, haydn_bin)


def resolve_haydn_bin(explicit: Optional[str]) -> Optional[Path]:
    candidates: List[str] = []
    if explicit:
        candidates.append(explicit)
    for key in ("HAYDN_BIN", "BUNDLESIM_HAYDN_TOOLCHAIN_BIN"):
        val = os.environ.get(key)
        if val:
            candidates.append(val)
    candidates.append("/ssd2/mhyang/haydn-build/bin")
    for cand in candidates:
        bin_dir = Path(cand)
        if (bin_dir / "clang").exists() or (bin_dir / "llc").exists():
            return bin_dir.resolve()
    return None


def resolve_consumer_recorder(bundlesim: Optional[Path]) -> Optional[Path]:
    if bundlesim is None:
        return None
    path = bundlesim / "scripts" / "record_haydn_artifact_set.py"
    return path if path.is_file() else None


def sysroot_hygiene_errors(sysroot: Path) -> List[str]:
    """Refuse stale .bak / .broken* debris next to the product ld."""
    found: List[str] = []
    lib = sysroot / "lib"
    roots = [lib] if lib.is_dir() else [sysroot]
    for root in roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            name = path.name
            if name.endswith(HYGIENE_SUFFIXES) or any(
                part in name for part in HYGIENE_NAME_PARTS
            ):
                try:
                    found.append(str(path.relative_to(sysroot)))
                except ValueError:
                    found.append(str(path))
    if found:
        return ["sysroot.hygiene:" + ",".join(sorted(found)[:8])]
    return []


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


def artifact_product_ld_errors(sysroot: Optional[Path]) -> List[str]:
    """Fail-closed: live ARTIFACT.product_ld must be the in-tree bind."""
    if sysroot is None:
        return []
    stamp = sysroot / "ARTIFACT.json"
    if not stamp.is_file():
        return []
    try:
        record = json.loads(stamp.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"ARTIFACT.json unreadable: {exc}"]
    if not isinstance(record, dict):
        return ["ARTIFACT.json is not an object"]
    block = record.get("product_ld")
    if not isinstance(block, dict) or not block:
        return [
            "ARTIFACT.product_ld missing (null is fail-closed; "
            "bind with record_haydn_artifact_set.py --install-product-ld)"
        ]
    errs: List[str] = []
    if (block.get("authority") or "") != "haydn-rt/haydn.ld":
        errs.append("ARTIFACT.product_ld.authority")
    if block.get("complete") is not True or block.get("body_match") is not True:
        errs.append("ARTIFACT.product_ld incomplete or body mismatch")
    return errs


def artifact_rebind_errors(
    sysroot: Optional[Path], llvm_src: Optional[Path]
) -> List[str]:
    """Post-wave rebind: refuse 28700d57; live commit must be repo-resident."""
    if sysroot is None:
        return []
    stamp = sysroot / "ARTIFACT.json"
    if not stamp.is_file():
        return []
    try:
        record = json.loads(stamp.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"ARTIFACT.json unreadable: {exc}"]
    if not isinstance(record, dict):
        return ["ARTIFACT.json is not an object"]
    return llvm_src_rebind_errors(record, llvm_src)


def llvm_src_rebind_errors(
    record: Dict[str, Any], llvm_src: Optional[Path]
) -> List[str]:
    llvm_block = record.get("llvm_src") or {}
    commit = ""
    if isinstance(llvm_block, dict):
        commit = str(llvm_block.get("git_commit") or "")
    elif isinstance(llvm_block, str):
        commit = llvm_block
    if not commit:
        return ["artifact.llvm_src.git_commit.missing"]
    if commit.lower().startswith(STALE_FULL_GATE_COMMIT):
        return [
            "artifact.llvm_src.git_commit.stale_28700d57 "
            f"(rebind to in-repo {REBIND_ANCESTOR})"
        ]
    if llvm_src is not None and _git_work_tree(llvm_src):
        if not git_commit_is_repo_resident(llvm_src, commit):
            return [
                "artifact.llvm_src.git_commit.not_repo_resident "
                f"({commit}; refuse {STALE_FULL_GATE_COMMIT}; "
                f"pin {REBIND_ANCESTOR})"
            ]
    return []


def check_monorepo_product_ld_install(llvm_src: Path) -> List[str]:
    """Fail-closed: this recorder is the product-ld install path."""
    path = Path(__file__).resolve()
    cand = llvm_src / "llvm" / "utils" / "haydn" / "record_haydn_artifact_set.py"
    if cand.is_file():
        path = cand
    if not path.is_file():
        return ["record_haydn_artifact_set.py missing"]
    text = path.read_text(encoding="utf-8", errors="replace")
    errs: List[str] = []
    if "haydn-rt/haydn.ld" not in text:
        errs.append("record_haydn_artifact_set.py does not bind haydn-rt/haydn.ld")
    if "def install_product_ld" not in text:
        errs.append("record_haydn_artifact_set.py has no product-ld install path")
    if ".bak" not in text or ".broken" not in text:
        errs.append("record_haydn_artifact_set.py missing .bak/.broken refuse")
    return errs


def _product_ld_script_errors(text: str, label: str) -> List[str]:
    """Shared haydn.ld + debris needles. BSP plat/crt/sys may still ship."""
    errs: List[str] = []
    if "haydn-rt/haydn.ld" not in text:
        errs.append(f"{label} does not bind haydn-rt/haydn.ld")
    if "PRODUCT_LD" not in text and "_product_ld" not in text:
        errs.append(f"{label} has no product-ld install path")
    if ".bak" not in text or ".broken" not in text:
        errs.append(f"{label} missing .bak/.broken refuse")
    return errs


def read_committed_install_script(bundlesim: Path) -> Optional[str]:
    """HEAD:scripts/install_haydn_sysroot.sh. Working-tree bind is not landed."""
    try:
        return subprocess.check_output(
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
        return None


def check_install_script(
    bundlesim: Optional[Path], *, committed: bool = False
) -> List[str]:
    """Consumer-side bind: install writes haydn-rt/haydn.ld and refuses debris."""
    if bundlesim is None:
        return []
    if committed:
        text = read_committed_install_script(bundlesim)
        if text is None:
            return ["missing committed install_haydn_sysroot.sh"]
        return _product_ld_script_errors(
            text, "committed install_haydn_sysroot.sh"
        )
    path = bundlesim / INSTALL_REL
    if not path.is_file():
        return [f"missing {path}"]
    text = path.read_text(encoding="utf-8", errors="replace")
    return _product_ld_script_errors(text, "install_haydn_sysroot.sh")


def run_consumer_install_self_test(bundlesim: Optional[Path]) -> List[str]:
    """Execute working-tree refuse + bind. No clang, no libc rebuild."""
    if bundlesim is None:
        return []
    path = bundlesim / INSTALL_REL
    if not path.is_file():
        return [f"missing {path}"]
    text = path.read_text(encoding="utf-8", errors="replace")
    if "--self-test" not in text or "_self_test_product_ld" not in text:
        return ["install_haydn_sysroot.sh missing product-ld --self-test"]
    try:
        out = subprocess.check_output(
            ["bash", str(path), "--self-test"],
            stderr=subprocess.STDOUT,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        detail = ""
        if isinstance(exc, subprocess.CalledProcessError) and exc.output:
            detail = " " + exc.output.strip().splitlines()[-1]
        return [f"install_haydn_sysroot.sh --self-test failed{detail}"]
    if "product-ld self-test OK" not in out:
        return ["install_haydn_sysroot.sh --self-test missing OK line"]
    return []


def install_product_ld(llvm_src: Path, sysroot: Path) -> Dict[str, Any]:
    """Bind haydn-rt/haydn.ld as lib/haydn.ld + lib/bundlesim.ld.

    Refuses stale .bak/.broken debris. Does not rebuild libc.
    """
    hygiene = sysroot_hygiene_errors(sysroot)
    if hygiene:
        raise RuntimeError("; ".join(hygiene))
    product = llvm_src / PRODUCT_REL
    if not product.is_file():
        raise FileNotFoundError(f"missing {product}")
    text = product.read_text(encoding="utf-8")
    lib = sysroot / "lib"
    lib.mkdir(parents=True, exist_ok=True)
    for name in SYSROOT_LD_NAMES:
        (lib / name).write_text(text, encoding="utf-8")
    return build_product_ld_block(llvm_src, sysroot)


def build_product_ld_block(
    llvm_src: Path, sysroot: Optional[Path]
) -> Dict[str, Any]:
    in_tree = llvm_src / PRODUCT_REL
    text = in_tree.read_text(encoding="utf-8", errors="replace")
    body = product_ld.canonical_body(text)
    block: Dict[str, Any] = {
        "authority": "haydn-rt/haydn.ld",
        "in_tree": {
            "path": str(in_tree),
            "sha256": sha256_file(in_tree),
            "body_sha256": product_ld.sha256_text(body),
        },
        "sysroot": {},
        "body_match": False,
        "complete": False,
    }
    if sysroot is None:
        return block
    installed: Dict[str, Any] = {}
    match = True
    found = False
    for name in SYSROOT_LD_NAMES:
        path = sysroot / "lib" / name
        if not path.is_file():
            continue
        found = True
        sys_text = path.read_text(encoding="utf-8", errors="replace")
        sys_body = product_ld.canonical_body(sys_text)
        same = sys_body == body
        installed[name] = {
            "path": str(path.resolve()),
            "sha256": sha256_file(path),
            "body_sha256": product_ld.sha256_text(sys_body),
            "body_match": same,
        }
        if not same:
            match = False
    block["sysroot"] = installed
    block["body_match"] = bool(found and match)
    block["complete"] = bool(found and match)
    return block


def product_ld_errors(block: Dict[str, Any], *, require_sysroot: bool) -> List[str]:
    errs: List[str] = []
    if (block.get("authority") or "") != "haydn-rt/haydn.ld":
        errs.append("product_ld.authority")
    if not (block.get("in_tree") or {}).get("sha256"):
        errs.append("product_ld.in_tree.missing")
    installed = block.get("sysroot") or {}
    if not installed:
        if require_sysroot:
            errs.append("product_ld.sysroot.missing")
        return errs
    if "bundlesim.ld" not in installed:
        errs.append("product_ld.sysroot.bundlesim.ld.missing")
    if "haydn.ld" not in installed:
        errs.append("product_ld.sysroot.haydn.ld.missing")
    if not block.get("body_match"):
        errs.append("product_ld.body_mismatch")
    return errs


def build_library_block(
    llvm_src: Path, sysroot: Optional[Path]
) -> Dict[str, Any]:
    """Same-artifact libc/libm + product_library_pin identity. Not QUALIFY."""
    pin = NATUREDSP_PIN if NATUREDSP_PIN.is_file() else None
    residual = NATUREDSP_RESIDUAL if NATUREDSP_RESIDUAL.is_file() else None
    vec_dot16 = "residual"
    if residual is not None:
        text = residual.read_text(encoding="utf-8", errors="replace")
        if "vec_dot16" in text and "T-DSP3" in text:
            vec_dot16 = "residual"
    block: Dict[str, Any] = {
        "authority": "product_library_pin",
        "complete": False,
        "qualified": False,
        "semantic_qualify": False,
        "vec_dot16_exactness": vec_dot16,
        "vec_dot16_owner": "T-DSP3",
        "pin": str(pin) if pin is not None else None,
        "kpi": {
            "m2_resweep": "measured-miss",
            "per_op_resource_records_admitted": False,
            "complete_model": 0,
        },
        "coverage": {
            "approved_expand": APPROVED_EXPAND_COUNT,
            "beyond_approved": BEYOND_APPROVED_COUNT,
            "total_hifi3": TOTAL_HIFI3_COUNT,
            "authority": "product_library_pin",
            "qualified": False,
            "second_matrix": False,
            "tdsp12_default_cpu": "residual",
            "tdsp13_declared_vs_tested": "residual",
        },
        "sysroot": {},
        "llvm_src": str(llvm_src),
    }
    live = census_naturedsp_hifi3()
    if live is not None:
        block["coverage"] = live
    if sysroot is None:
        return block
    installed: Dict[str, Any] = {}
    have = True
    for name in SYSROOT_LIBS:
        path = sysroot / "lib" / name
        if not path.is_file():
            have = False
            installed[name] = {"present": False}
            continue
        installed[name] = {
            "path": str(path.resolve()),
            "sha256": sha256_file(path),
            "present": True,
        }
    block["sysroot"] = installed
    block["complete"] = bool(have)
    return block


def library_errors(block: Dict[str, Any], *, require_sysroot: bool) -> List[str]:
    errs: List[str] = []
    if (block.get("authority") or "") != "product_library_pin":
        errs.append("library.authority")
    if block.get("qualified") is True or block.get("semantic_qualify") is True:
        errs.append("library.false_qualified")
    if (block.get("vec_dot16_exactness") or "") != "residual":
        errs.append("library.vec_dot16.not_residual")
    if (block.get("vec_dot16_owner") or "") != "T-DSP3":
        errs.append("library.vec_dot16.owner")
    kpi = block.get("kpi") or {}
    if kpi.get("m2_resweep") != "measured-miss":
        errs.append("library.kpi.m2_not_measured_miss")
    if kpi.get("per_op_resource_records_admitted") is True:
        errs.append("library.kpi.false_admitted")
    cov = block.get("coverage") or {}
    if not cov:
        errs.append("library.coverage.missing")
    else:
        if cov.get("qualified") is True or cov.get("second_matrix") is True:
            errs.append("library.coverage.false_qualified")
        if int(cov.get("approved_expand") or 0) != APPROVED_EXPAND_COUNT:
            errs.append("library.coverage.approved_expand")
        if int(cov.get("beyond_approved") or 0) != BEYOND_APPROVED_COUNT:
            errs.append("library.coverage.beyond_approved")
        if int(cov.get("total_hifi3") or 0) != TOTAL_HIFI3_COUNT:
            errs.append("library.coverage.total_hifi3")
        if cov.get("tdsp12_default_cpu") != "residual":
            errs.append("library.coverage.tdsp12.false_green")
        if cov.get("tdsp13_declared_vs_tested") != "residual":
            errs.append("library.coverage.tdsp13.false_green")
    installed = block.get("sysroot") or {}
    if not installed:
        if require_sysroot:
            errs.append("library.sysroot.missing")
        return errs
    for name in SYSROOT_LIBS:
        info = installed.get(name) or {}
        if not info.get("present"):
            errs.append(f"library.{name}.missing")
    if require_sysroot and not block.get("complete"):
        errs.append("library.incomplete")
    return errs


def augment_record(
    record: Dict[str, Any], llvm_src: Path, sysroot: Optional[Path]
) -> Dict[str, Any]:
    record = dict(record)
    record["product_ld"] = build_product_ld_block(llvm_src, sysroot)
    record["library"] = build_library_block(llvm_src, sysroot)
    return record


def attach_owned_blocks(
    artifact: Path, llvm_src: Path, sysroot: Optional[Path]
) -> Dict[str, Any]:
    """Stamp product_ld + library onto an existing ARTIFACT.json.

    Does not rehash clang/llc/loader. Use after a consumer restamp so the
    monorepo-owned identity seats stay on the same tuple. Re-binds
    haydn-rt/haydn.ld into the sysroot and refuses .bak/.broken debris.
    """
    if sysroot is not None:
        install_product_ld(llvm_src, sysroot)
    record = json.loads(artifact.read_text(encoding="utf-8"))
    if not isinstance(record, dict):
        raise ValueError("ARTIFACT.json is not an object")
    record = augment_record(record, llvm_src, sysroot)
    text = json.dumps(record, indent=2, sort_keys=True)
    if not text.endswith("\n"):
        text += "\n"
    artifact.write_text(text, encoding="utf-8")
    return record


def run_consumer_recorder(recorder: Path, argv: List[str]) -> int:
    cmd = [sys.executable, str(recorder), *argv]
    return subprocess.call(cmd)


def _self_test() -> int:
    ident = "/* HAYDN-LD-IDENT: 2026-08-14 Format E 12-byte parcel */\n"
    body = (
        "OUTPUT_ARCH(haydn)\n"
        "ENTRY(_start)\n"
        "PROVIDE(BUNDLESIM_TEXT_BASE = 0x00010000);\n"
        "PROVIDE(BUNDLESIM_INSTR_ALIGN = 2);\n"
        "PROVIDE(BUNDLESIM_RECORD_BYTES = 12);\n"
        "PROVIDE(BUNDLESIM_ABI_ALIGN = 16);\n"
    )
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        product = root / "haydn-rt" / "haydn.ld"
        product.parent.mkdir()
        product.write_text(ident + body, encoding="utf-8")
        sysroot = root / "sysroot" / "haydn-unknown-elf"
        (sysroot / "lib").mkdir(parents=True)
        (sysroot / "lib" / "bundlesim.ld").write_text(ident + body, encoding="utf-8")
        (sysroot / "lib" / "haydn.ld").write_text(body, encoding="utf-8")
        block = build_product_ld_block(root, sysroot)
        errs = product_ld_errors(block, require_sysroot=True)
        assert not errs, errs
        assert block["complete"] and block["body_match"]

        (sysroot / "lib" / "bundlesim.ld").write_text(
            body.replace("0x00010000", "0x00020000"), encoding="utf-8"
        )
        block = build_product_ld_block(root, sysroot)
        errs = product_ld_errors(block, require_sysroot=True)
        assert any("body_mismatch" in e for e in errs), errs

        install = root / "scripts" / "install_haydn_sysroot.sh"
        install.parent.mkdir()
        install.write_text("echo bsp only\n", encoding="utf-8")
        inst_errs = check_install_script(root)
        assert inst_errs, inst_errs
        install.write_text(
            "# bind _product_ld from haydn-rt/haydn.ld\n"
            "_product_ld=...\n"
            "# refuse .bak / .broken debris\n",
            encoding="utf-8",
        )
        assert not check_install_script(root)
        # No git tree here: committed identity is unread (residual INFO path).
        assert check_install_script(root, committed=True)
        # --require-consumer-install fail-closes on working-tree + committed.

        libdir = sysroot / "lib"
        (libdir / "libc.a").write_bytes(b"libc")
        (libdir / "libm.a").write_bytes(b"libm")
        lib = build_library_block(root, sysroot)
        lib_errs = library_errors(lib, require_sysroot=True)
        assert not lib_errs, lib_errs
        assert lib["complete"] and lib["vec_dot16_exactness"] == "residual"
        assert (lib.get("coverage") or {}).get("beyond_approved") == BEYOND_APPROVED_COUNT
        (libdir / "libm.a").unlink()
        lib = build_library_block(root, sysroot)
        lib_errs = library_errors(lib, require_sysroot=True)
        assert any("libm.a.missing" in e for e in lib_errs), lib_errs

        (libdir / "libm.a").write_bytes(b"libm")
        (sysroot / "lib" / "bundlesim.ld").write_text(ident + body, encoding="utf-8")
        (sysroot / "lib" / "haydn.ld").write_text(body, encoding="utf-8")
        stamp = sysroot / "ARTIFACT.json"
        stamp.write_text(
            json.dumps(
                {
                    "artifact_id": "ab" * 32,
                    "product_ld": None,
                    "llvm_src": {
                        "git_commit": "28700d57366a35a7d04e8adfbdf782743ec847e0"
                    },
                }
            ),
            encoding="utf-8",
        )
        assert any("product_ld" in e for e in artifact_product_ld_errors(sysroot))
        assert any("28700d57" in e for e in artifact_rebind_errors(sysroot, root))
        attached = attach_owned_blocks(stamp, root, sysroot)
        assert attached.get("library", {}).get("complete") is True
        assert attached.get("product_ld", {}).get("body_match") is True
        reloaded = json.loads(stamp.read_text(encoding="utf-8"))
        assert "library" in reloaded and "product_ld" in reloaded
        assert not artifact_product_ld_errors(sysroot)
        # attach does not rewrite llvm_src; stale 28700d57 stays fail-closed.
        assert any("28700d57" in e for e in artifact_rebind_errors(sysroot, root))
        reloaded["llvm_src"] = {
            "git_commit": "4b6677f8bf87d12f20e4d0b0ff5ec10124dddf2f"
        }
        stamp.write_text(json.dumps(reloaded), encoding="utf-8")
        live_repo = monorepo_from_script()
        if _git_work_tree(live_repo):
            assert not artifact_rebind_errors(sysroot, live_repo)
            assert git_commit_is_repo_resident(live_repo, REBIND_ANCESTOR)
            assert not git_commit_is_repo_resident(
                live_repo, "28700d57366a35a7d04e8adfbdf782743ec847e0"
            )

        debris = sysroot / "lib" / "stale.ld.bak"
        debris.write_text("x", encoding="utf-8")
        try:
            install_product_ld(root, sysroot)
            raise AssertionError("expected hygiene refuse")
        except RuntimeError as exc:
            assert "hygiene" in str(exc)
        debris.unlink()
        (sysroot / "lib" / "bundlesim.ld").write_text("old\n", encoding="utf-8")
        installed = install_product_ld(root, sysroot)
        assert installed["complete"] and installed["body_match"]
        assert not check_monorepo_product_ld_install(monorepo_from_script())

    print("record_haydn_artifact_set self-test OK")
    return 0


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument(
        "--check-product-ld",
        action="store_true",
        help="pin in-tree haydn.ld and optional sysroot body match",
    )
    ap.add_argument(
        "--check-library",
        action="store_true",
        help="pin sysroot libc.a+libm.a and product_library_pin residual",
    )
    ap.add_argument(
        "--attach-owned",
        action="store_true",
        help="stamp product_ld+library onto existing $sysroot/ARTIFACT.json",
    )
    ap.add_argument(
        "--install-product-ld",
        action="store_true",
        help="bind haydn-rt/haydn.ld into sysroot and attach ARTIFACT.product_ld",
    )
    ap.add_argument(
        "--require-sysroot",
        action="store_true",
        help="fail --check-product-ld when sysroot ld is absent",
    )
    ap.add_argument(
        "--require-consumer-install",
        action="store_true",
        help="fail --check-product-ld when the BundleSim working-tree install is unbound",
    )
    ap.add_argument("--out", type=Path)
    ap.add_argument("--restamp", action="store_true")
    ap.add_argument("--pretty", action="store_true")
    ap.add_argument("--haydn-bin", default=None)
    ap.add_argument("--sysroot", type=Path, default=None)
    ap.add_argument("--golden-dir", default=None)
    ap.add_argument("--llvm-src", type=Path, default=None)
    ap.add_argument("--bundlesim", type=Path, default=None)
    ap.add_argument(
        "--components-match",
        nargs=2,
        metavar=("STAMP", "LIVE"),
    )
    ap.add_argument("--check-debug", metavar="STAMP")
    args = ap.parse_args(argv)

    if args.self_test:
        return _self_test()

    haydn_bin = resolve_haydn_bin(args.haydn_bin)
    try:
        llvm_src = resolve_llvm_src(args.llvm_src, haydn_bin)
    except FileNotFoundError as exc:
        print(f"record_haydn_artifact_set: FAIL {exc}", file=sys.stderr)
        return 1
    sysroot = resolve_sysroot(args.sysroot, haydn_bin)
    bundlesim = resolve_bundlesim(args.bundlesim)
    consumer = resolve_consumer_recorder(bundlesim)

    if args.check_product_ld:
        block = build_product_ld_block(llvm_src, sysroot)
        errs = product_ld_errors(
            block, require_sysroot=args.require_sysroot or sysroot is not None
        )
        errs.extend(check_monorepo_product_ld_install(llvm_src))
        if sysroot is not None:
            errs.extend(sysroot_hygiene_errors(sysroot))
            errs.extend(artifact_product_ld_errors(sysroot))
            errs.extend(artifact_rebind_errors(sysroot, llvm_src))
        consumer_errs = check_install_script(bundlesim)
        committed_errs = check_install_script(bundlesim, committed=True)
        if args.require_consumer_install:
            # T7-RT LANDED (edbb813): working-tree + committed HEAD + self-test.
            errs.extend(consumer_errs)
            errs.extend(committed_errs)
            errs.extend(run_consumer_install_self_test(bundlesim))
            if not committed_errs:
                print("  PASS: T7-RT committed install binds haydn-rt/haydn.ld")
        else:
            if committed_errs:
                for e in committed_errs:
                    print(f"  INFO: T7-RT residual (committed checkout OPEN): {e}")
            else:
                print("  PASS: T7-RT committed install binds haydn-rt/haydn.ld")
            for e in consumer_errs:
                if e not in committed_errs:
                    print(f"  INFO: consumer-install working-tree: {e}")
        print(
            "record_haydn_artifact_set: "
            f"in_tree={block['in_tree'].get('path')} "
            f"body_sha256={block['in_tree'].get('body_sha256')} "
            f"body_match={block.get('body_match')}"
        )
        if errs:
            for e in errs:
                print(f"  FAIL: {e}", file=sys.stderr)
            print("record_haydn_artifact_set: FAIL product-ld", file=sys.stderr)
            return 1
        print("record_haydn_artifact_set: PASS product-ld bind")
        return 0

    if args.check_library:
        block = build_library_block(llvm_src, sysroot)
        errs = library_errors(
            block, require_sysroot=args.require_sysroot or sysroot is not None
        )
        print(
            "record_haydn_artifact_set: "
            f"authority={block.get('authority')} "
            f"complete={block.get('complete')} "
            f"vec_dot16={block.get('vec_dot16_exactness')} "
            f"m2={((block.get('kpi') or {}).get('m2_resweep'))}"
        )
        if errs:
            for e in errs:
                print(f"  FAIL: {e}", file=sys.stderr)
            print("record_haydn_artifact_set: FAIL library", file=sys.stderr)
            return 1
        print("record_haydn_artifact_set: PASS library identity")
        return 0

    if args.install_product_ld or args.attach_owned:
        if sysroot is None:
            print("record_haydn_artifact_set: FAIL install/attach needs sysroot", file=sys.stderr)
            return 1
        stamp = sysroot / "ARTIFACT.json"
        label = "install-product-ld" if args.install_product_ld else "attach-owned"
        try:
            if stamp.is_file():
                record = attach_owned_blocks(stamp, llvm_src, sysroot)
            else:
                install_product_ld(llvm_src, sysroot)
                if args.attach_owned:
                    print(f"record_haydn_artifact_set: FAIL missing {stamp}", file=sys.stderr)
                    return 1
                print(
                    "record_haydn_artifact_set: PASS install-product-ld "
                    f"(no ARTIFACT.json at {sysroot})"
                )
                return 0
        except (OSError, ValueError, json.JSONDecodeError, RuntimeError) as exc:
            print(f"record_haydn_artifact_set: FAIL {label}: {exc}", file=sys.stderr)
            return 1
        errs = product_ld_errors(record.get("product_ld") or {}, require_sysroot=True)
        errs.extend(library_errors(record.get("library") or {}, require_sysroot=True))
        errs.extend(sysroot_hygiene_errors(sysroot))
        errs.extend(llvm_src_rebind_errors(record, llvm_src))
        if errs:
            for e in errs:
                print(f"  FAIL: {e}", file=sys.stderr)
            print(f"record_haydn_artifact_set: FAIL {label}", file=sys.stderr)
            return 1
        print(
            f"record_haydn_artifact_set: PASS {label} "
            f"artifact_id={record.get('artifact_id', '')} "
            f"library={record.get('library', {}).get('complete')} "
            f"product_ld={record.get('product_ld', {}).get('body_match')}"
        )
        return 0

    # Hashing / restamp / debug / components-match stay on the consumer
    # recorder so there is one artifact_id algorithm.
    if consumer is None:
        print(
            "record_haydn_artifact_set: FAIL consumer recorder missing "
            "(BundleSim/scripts/record_haydn_artifact_set.py)",
            file=sys.stderr,
        )
        return 1

    if args.components_match is not None or args.check_debug is not None:
        passthrough: List[str] = []
        if args.components_match is not None:
            passthrough.extend(
                ["--components-match", str(args.components_match[0]),
                 str(args.components_match[1])]
            )
        if args.check_debug is not None:
            passthrough.extend(["--check-debug", args.check_debug])
        return run_consumer_recorder(consumer, passthrough)

    delegate_argv: List[str] = []
    if args.restamp:
        delegate_argv.append("--restamp")
    if args.pretty:
        delegate_argv.append("--pretty")
    if args.out is not None:
        delegate_argv.extend(["--out", str(args.out)])
    if args.haydn_bin:
        delegate_argv.extend(["--haydn-bin", args.haydn_bin])
    if args.sysroot is not None:
        delegate_argv.extend(["--sysroot", str(args.sysroot)])
    if args.golden_dir:
        delegate_argv.extend(["--golden-dir", args.golden_dir])
    if args.llvm_src is not None:
        delegate_argv.extend(["--llvm-src", str(args.llvm_src)])
    if args.bundlesim is not None:
        delegate_argv.extend(["--bundlesim", str(args.bundlesim)])

    # Always write a temp JSON so this seat can attach product_ld.
    with tempfile.TemporaryDirectory() as tmp:
        tmp_out = Path(tmp) / "ARTIFACT.json"
        run_argv = list(delegate_argv)
        if "--out" not in run_argv:
            run_argv.extend(["--out", str(tmp_out)])
            if args.pretty or args.restamp:
                if "--pretty" not in run_argv:
                    run_argv.append("--pretty")
        rc = run_consumer_recorder(consumer, run_argv)
        if rc != 0:
            return rc
        out_path = args.out
        if args.restamp and out_path is None:
            if sysroot is None:
                print(
                    "record_haydn_artifact_set: FAIL restamp needs sysroot",
                    file=sys.stderr,
                )
                return 1
            out_path = sysroot / "ARTIFACT.json"
        written = Path(args.out) if args.out is not None else tmp_out
        if args.out is None and args.restamp:
            written = out_path
        try:
            record = json.loads(written.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            # Delegate may have written only to --out we injected.
            try:
                record = json.loads(tmp_out.read_text(encoding="utf-8"))
                written = tmp_out
            except (OSError, json.JSONDecodeError):
                print(
                    f"record_haydn_artifact_set: FAIL read delegate stamp: {exc}",
                    file=sys.stderr,
                )
                return 1
        if sysroot is not None:
            try:
                install_product_ld(llvm_src, sysroot)
            except RuntimeError as exc:
                print(f"record_haydn_artifact_set: FAIL product-ld install: {exc}", file=sys.stderr)
                return 1
        record = augment_record(record, llvm_src, sysroot)
        errs = product_ld_errors(
            record["product_ld"],
            require_sysroot=args.restamp or sysroot is not None,
        )
        errs.extend(
            library_errors(
                record["library"],
                require_sysroot=args.restamp or sysroot is not None,
            )
        )
        if sysroot is not None:
            errs.extend(sysroot_hygiene_errors(sysroot))
        errs.extend(llvm_src_rebind_errors(record, llvm_src))
        if args.require_consumer_install:
            errs.extend(check_install_script(bundlesim))
            errs.extend(run_consumer_install_self_test(bundlesim))
        if errs:
            for e in errs:
                print(f"  FAIL: {e}", file=sys.stderr)
            print(
                "record_haydn_artifact_set: FAIL product-ld after restamp",
                file=sys.stderr,
            )
            return 1
        text = json.dumps(
            record,
            indent=2 if (args.pretty or args.restamp) else None,
            sort_keys=bool(args.pretty or args.restamp),
            separators=None if (args.pretty or args.restamp) else (",", ":"),
        )
        if not text.endswith("\n"):
            text += "\n"
        dest = out_path if out_path is not None else None
        if dest is not None:
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(text, encoding="utf-8")
            print(
                f"OK: artifact_id={record.get('artifact_id', '')} "
                f"product_ld=haydn-rt/haydn.ld → {dest}",
                file=sys.stderr,
            )
        else:
            sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
