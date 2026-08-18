#!/usr/bin/env python3
"""Monorepo same-artifact ordinary-scheduler + StageCount1 SMS measurement seat.

Owns same-artifact structural observation for ordinary list-schedule/commit and
soft StageCount1 SMS. Dual-run freestanding-12 corpus. No peer thresholds.
Per-op resource import stays fail-closed until golden admits the complete table
(per_op_resource_records_admitted=false; CompleteModel=0).

Collects:
  * textual -S brace / occupancy / SWPS polarity (structural)
  * object .text bytes via llvm-size (P8 parcel geometry: 12-byte Format E)
  * compiler binary identity (path + --version + sha256 when available)
  * golden-hash binding on every seat from live/sysroot ARTIFACT.json and/or
    BundleSim record_haydn_artifact_set.py
  * mandatory semantic host execution via BundleSim run_c when the product
    host is present (guest exit 0 + committed_bundles functional count)
  * shared availability-aware aggregate resource surface (port ceilings /
    issue width / exec units) and ordinary lifecycle STATS evidence when
    asserts are available
  * optional product artifact stamp via BundleSim record_haydn_artifact_set.py

Competitive II/density claims remain closed. Semantic host and golden-hash
binding are mandatory whenever the product host / ARTIFACT seats are present;
structural-only self-test remains available without BundleSim. Sequentialize
is recovery-only — product coissue probe is the packing legality authority.
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
from typing import Any

PARCEL_BYTES = 12
RE_BUNDLE = re.compile(r"^\s*\{\s*(.*?)\s*\}\s*$")
RE_SWPS = re.compile(r"#<swps>")
RE_SWPS_STAGES = re.compile(r"#<swps>[^\n]*stages=(\d+)")
RE_FUNC = re.compile(r"^([A-Za-z_][A-Za-z0-9_.$]*):")
RE_SPILL_ST = re.compile(r"\b(st32|st16|st8|st64|st)\b.*\bsp\b", re.I)
RE_SPILL_LD = re.compile(r"\b(ld32|ld16|ld8|ld64|ld)\b.*\bsp\b", re.I)
RE_NOP = re.compile(r"^\s*nop\b", re.I)
RE_SIZE_TEXT = re.compile(r"^\.text\s+(\d+)\b", re.M)
RE_RUN_C_STOP = re.compile(
    r"stop=(\S+)\s+guest_exit=(-?\d+)\s+bundles=(\d+)"
)
RE_WORK_KEPT = re.compile(r"work dir kept:\s+(\S+)")
RE_STAT_LINE = re.compile(
    r"^\s*(\d+)\s+haydn-(post-ra-sched|prera-sched)\s*-\s*(.+?)\s*$"
)


GOLDEN_FILES = (
    "format_e_bit_layout_v2_1.xlsx",
    "format_e_bit_layout_v2_1.json",
    "format_e_canonical_vectors_v1.json",
    "VLIW_Engine_Compiler_Constraints.md",
    "VLIW_Engine_Reference_Manual.docx",
)

MONOREPO = Path(__file__).resolve().parents[2]
CANARIES = {
    "mac_loop": "int mac_loop(const int *x,const int *h,int n){int a=0;for(int i=0;i<n;++i)a+=x[i]*h[i];return a;}\n",
    "fir_taps4": "void fir_taps4(const short *x,const short *h,short *y,int n){for(int i=0;i<n;++i){int a=0;for(int k=0;k<4;++k)a+=(int)x[i+k]*(int)h[k];y[i]=(short)(a>>15);}}\n",
    "soft_countdown": "int soft_countdown(int x){int s=x;for(int k=32;k!=0;--k){s=s*3+5;s=s*7+11;s=s*13+17;s^=x;}return s;}\n",
    "vec_add": "void vec_add(const int *a,const int *b,int *c,int n){for(int i=0;i<n;++i)c[i]=a[i]+b[i];}\n",
    "ilp_three_add": "void ilp_three_add(int a,int b,int c,int d,int e,int f,int *p1,int *p2,int *p3){int x=a+b,y=c+d,z=e+f;*p1=x;*p2=y;*p3=z;}\n",
    "crit_chain": "int crit_chain(int x){int a=x+1,b=a*3,c=b+7,d=c*5;return d^x;}\n",
    "dot_product": "int dot_product(const int *a,const int *b,int n){int acc=0;for(int i=0;i<n;++i)acc+=a[i]*b[i];return acc;}\n",
    "saxpy": "void saxpy(int a,const int *x,int *y,int n){for(int i=0;i<n;++i)y[i]=a*x[i]+y[i];}\n",
    "iir_one_pole": "int iir_one_pole(const int *x,int *y,int n,int a,int b){int s=0;for(int i=0;i<n;++i){s=a*x[i]+b*s;y[i]=s;}return s;}\n",
    "accum_ilp4": "int accum_ilp4(const int *p,int n){int a=0,b=0,c=0,d=0;for(int i=0;i+3<n;i+=4){a+=p[i];b+=p[i+1];c+=p[i+2];d+=p[i+3];}return a+b+c+d;}\n",
    "memcpy_words": "void memcpy_words(int *dst,const int *src,int n){for(int i=0;i<n;++i)dst[i]=src[i];}\n",
    "minmax_scan": "void minmax_scan(const int *p,int n,int *omin,int *omax){int lo=p[0],hi=p[0];for(int i=1;i<n;++i){int v=p[i];if(v<lo)lo=v;if(v>hi)hi=v;}*omin=lo;*omax=hi;}\n",
}
REQUIRED_CORPUS = tuple(CANARIES.keys())

SEMANTIC_DRIVERS: dict[str, str] = {
    "mac_loop": (
        "int mac_loop(const int *x,const int *h,int n){"
        "int a=0;for(int i=0;i<n;++i)a+=x[i]*h[i];return a;}\n"
        "int main(void){static const int x[4]={1,2,3,4};"
        "static const int h[4]={5,6,7,8};"
        "return mac_loop(x,h,4)==70?0:1;}\n"
    ),
    "fir_taps4": (
        "void fir_taps4(const short *x,const short *h,short *y,int n){"
        "for(int i=0;i<n;++i){int a=0;for(int k=0;k<4;++k)"
        "a+=(int)x[i+k]*(int)h[k];y[i]=(short)(a>>15);}}\n"
        "int main(void){static const short x[8]={100,200,300,400,500,600,700,800};"
        "static const short h[4]={1000,2000,3000,4000};short y[4];"
        "fir_taps4(x,h,y,4);"
        "return (y[0]==91&&y[1]==122&&y[2]==152&&y[3]==183)?0:1;}\n"
    ),
    "soft_countdown": (
        "int soft_countdown(int x){int s=x;for(int k=32;k!=0;--k){"
        "s=s*3+5;s=s*7+11;s=s*13+17;s^=x;}return s;}\n"
        "int main(void){return soft_countdown(3)==-1360750333?0:1;}\n"
    ),
    "vec_add": (
        "void vec_add(const int *a,const int *b,int *c,int n){"
        "for(int i=0;i<n;++i)c[i]=a[i]+b[i];}\n"
        "int main(void){static const int a[4]={1,2,3,4};"
        "static const int b[4]={10,20,30,40};int c[4];"
        "vec_add(a,b,c,4);"
        "return (c[0]==11&&c[1]==22&&c[2]==33&&c[3]==44)?0:1;}\n"
    ),
    "ilp_three_add": (
        "void ilp_three_add(int a,int b,int c,int d,int e,int f,"
        "int *p1,int *p2,int *p3){int x=a+b,y=c+d,z=e+f;*p1=x;*p2=y;*p3=z;}\n"
        "int main(void){int p1,p2,p3;ilp_three_add(1,2,3,4,5,6,&p1,&p2,&p3);"
        "return (p1==3&&p2==7&&p3==11)?0:1;}\n"
    ),
    "crit_chain": (
        "int crit_chain(int x){int a=x+1,b=a*3,c=b+7,d=c*5;return d^x;}\n"
        "int main(void){return crit_chain(7)==156?0:1;}\n"
    ),
    "dot_product": (
        "int dot_product(const int *a,const int *b,int n){"
        "int acc=0;for(int i=0;i<n;++i)acc+=a[i]*b[i];return acc;}\n"
        "int main(void){static const int a[4]={1,2,3,4};"
        "static const int b[4]={5,6,7,8};"
        "return dot_product(a,b,4)==70?0:1;}\n"
    ),
    "saxpy": (
        "void saxpy(int a,const int *x,int *y,int n){"
        "for(int i=0;i<n;++i)y[i]=a*x[i]+y[i];}\n"
        "int main(void){static const int x[4]={1,2,3,4};"
        "int y[4]={10,20,30,40};saxpy(3,x,y,4);"
        "return (y[0]==13&&y[1]==26&&y[2]==39&&y[3]==52)?0:1;}\n"
    ),
    "iir_one_pole": (
        "int iir_one_pole(const int *x,int *y,int n,int a,int b){"
        "int s=0;for(int i=0;i<n;++i){s=a*x[i]+b*s;y[i]=s;}return s;}\n"
        "int main(void){static const int x[4]={1,2,3,4};int y[4];"
        "int s=iir_one_pole(x,y,4,2,3);"
        "return (s==116&&y[0]==2&&y[1]==10&&y[2]==36&&y[3]==116)?0:1;}\n"
    ),
    "accum_ilp4": (
        "int accum_ilp4(const int *p,int n){int a=0,b=0,c=0,d=0;"
        "for(int i=0;i+3<n;i+=4){a+=p[i];b+=p[i+1];c+=p[i+2];d+=p[i+3];}"
        "return a+b+c+d;}\n"
        "int main(void){static const int p[8]={1,2,3,4,5,6,7,8};"
        "return accum_ilp4(p,8)==36?0:1;}\n"
    ),
    "memcpy_words": (
        "void memcpy_words(int *dst,const int *src,int n){"
        "for(int i=0;i<n;++i)dst[i]=src[i];}\n"
        "int main(void){static const int src[4]={9,8,7,6};int dst[4]={0};"
        "memcpy_words(dst,src,4);"
        "return (dst[0]==9&&dst[1]==8&&dst[2]==7&&dst[3]==6)?0:1;}\n"
    ),
    "minmax_scan": (
        "void minmax_scan(const int *p,int n,int *omin,int *omax){"
        "int lo=p[0],hi=p[0];for(int i=1;i<n;++i){int v=p[i];"
        "if(v<lo)lo=v;if(v>hi)hi=v;}*omin=lo;*omax=hi;}\n"
        "int main(void){static const int p[4]={9,1,5,3};int lo,hi;"
        "minmax_scan(p,4,&lo,&hi);return (lo==1&&hi==9)?0:1;}\n"
    ),
}

PRODUCT_SMS_CONTAINMENT_MAX_STAGE_COUNT = 1
PER_OP_RESOURCE_RECORDS_ADMITTED = False
COMPETITIVE_II_DENSITY_CLAIMS = False
M18_UNCLAIMABLE = True
NAT_IPC_MEASURED_MISS = True
SWPS_ASM_MEASURED_MISS = True
SF1_SF3_GATES_CLOSED = False
STAGE0_IB_PP_REVIVE = False
THREE_ARM = "ordinary,stagecount1,multistage"
# M2 / M17 / M18 / M19–M22 stay observation seats. No competitive
# II / density / NAT-IPC / SWPS-asm claim until admission.
M_EVAL_ONLY = {
    "M2": True,
    "M17": True,
    "M18": True,
    "M19": True,
    "M20": True,
    "M21": True,
    "M22": True,
}
IPC_PROXY_MEASURED_MISS = {
    "measured_miss": True,
    "competitive_claim": False,
    "same_artifact": True,
    "reason": "ipc_proxy is same-artifact observation only; CompleteModel=0",
}

# Golden-admitted aggregate surface (constraints port table + SchedModel).
# Mirrors HaydnPortModel / HaydnSchedule.td; not a competitive per-op invent.
AGGREGATE_RESOURCE_SURFACE = {
    "gpr_read_ports": 4,
    "gpr_write_ports": 2,
    "dr_read_ports": 7,
    "dr_write_ports": 3,
    "ar_read_ports": 2,
    "ar_write_ports": 2,
    "sfr_read_ports": 2,
    "sfr_write_ports": 1,
    "issue_width": 3,
    "num_exec_units": 7,
    "load_latency_scaffold": 2,
    "alone_in_cycle_opcodes": ["ARCTAN", "SIN_COS"],
}

PER_OP_AVAILABILITY_SAMPLES = (
    {"opcode": "ADD32", "availability": "AggregateCeilingsOnly",
     "competitive_claims_allowed": False, "issues_alone_in_cycle": False},
    {"opcode": "LD32", "availability": "AggregateCeilingsOnly",
     "competitive_claims_allowed": False, "issues_alone_in_cycle": False},
    {"opcode": "ARCTAN", "availability": "AggregateCeilingsOnly",
     "competitive_claims_allowed": False, "issues_alone_in_cycle": True},
    {"opcode": "SIN_COS", "availability": "AggregateCeilingsOnly",
     "competitive_claims_allowed": False, "issues_alone_in_cycle": True},
)


def haydn_bin() -> Path:
    env = os.environ.get("HAYDN_BIN") or os.environ.get(
        "BUNDLESIM_HAYDN_TOOLCHAIN_BIN"
    )
    return Path(env) if env else Path("/ssd2/mhyang/haydn-build/bin")


def file_sha256(path: Path) -> str | None:
    if not path.is_file():
        return None
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def compiler_identity(hb: Path) -> dict[str, Any]:
    clang = hb / "clang"
    row: dict[str, Any] = {
        "clang_path": str(clang),
        "clang_exists": clang.is_file(),
        "clang_sha256": file_sha256(clang) if clang.is_file() else None,
    }
    if not clang.is_file():
        return row
    try:
        r = subprocess.run(
            [str(clang), "--version"],
            capture_output=True,
            text=True,
            timeout=30,
        )
        ver = (r.stdout or r.stderr or "").strip().splitlines()
        row["clang_version"] = ver[0] if ver else ""
        row["clang_version_rc"] = r.returncode
    except (OSError, subprocess.TimeoutExpired) as exc:
        row["clang_version_error"] = str(exc)
    return row


def measure_asm(text: str) -> dict[str, Any]:
    bundles = real_ops = nops = spill_st = spill_ld = swps_lines = sc1 = ms = 0
    funcs: list[str] = []
    for line in text.splitlines():
        if RE_SWPS.search(line):
            swps_lines += 1
            m = RE_SWPS_STAGES.search(line)
            if m:
                st = int(m.group(1))
                sc1 += st == 1
                ms += st > 1
        line_nc = re.sub(r"//.*$", "", line).rstrip()
        mf = RE_FUNC.match(line_nc)
        if mf and not line_nc.startswith("."):
            funcs.append(mf.group(1))
        if RE_SPILL_ST.search(line_nc):
            spill_st += 1
        if RE_SPILL_LD.search(line_nc):
            spill_ld += 1
        m = RE_BUNDLE.match(line_nc)
        if not m:
            continue
        bundles += 1
        for part in [x.strip() for x in m.group(1).split(";")]:
            if not part:
                continue
            if part == "nop" or RE_NOP.match(part):
                nops += 1
            else:
                real_ops += 1
    slots = real_ops + nops
    return {
        "emitted_cycles": bundles,
        "bundles": bundles,
        "real_ops": real_ops,
        "nops": nops,
        "slots_total": slots,
        "occupancy": round((real_ops / slots) if slots else 0.0, 4),
        "enc_fill": round((real_ops / bundles) if bundles else 0.0, 4),
        "text_bytes_est": bundles * PARCEL_BYTES,
        "parcel_bytes": PARCEL_BYTES,
        "spill_store_lines": spill_st,
        "spill_load_lines": spill_ld,
        "swps_comment_lines": swps_lines,
        "swps_stagecount1": sc1,
        "swps_multistage": ms,
        "funcs": funcs,
    }


def measure_object_text_bytes(obj: Path, size_bin: Path) -> dict[str, Any]:
    out: dict[str, Any] = {
        "obj_path": str(obj),
        "obj_ok": False,
        "text_bytes_obj": None,
        "p8_parcel_aligned": None,
        "obj_sha256": file_sha256(obj) if obj.is_file() else None,
    }
    if not obj.is_file() or not size_bin.is_file():
        out["err"] = "missing obj or llvm-size"
        return out
    try:
        r = subprocess.run(
            [str(size_bin), "-A", str(obj)],
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        out["err"] = str(exc)
        return out
    if r.returncode != 0:
        out["err"] = (r.stderr or r.stdout)[-400:]
        return out
    m = RE_SIZE_TEXT.search(r.stdout or "")
    if not m:
        out["err"] = "no .text in llvm-size -A"
        out["size_raw"] = (r.stdout or "")[-400:]
        return out
    tb = int(m.group(1))
    out["obj_ok"] = True
    out["text_bytes_obj"] = tb
    out["p8_parcel_aligned"] = (tb % PARCEL_BYTES) == 0
    out["p8_parcel_count_obj"] = (
        tb // PARCEL_BYTES if tb % PARCEL_BYTES == 0 else None
    )
    return out


def compile_kernel(
    name: str, src: str, work: Path, clang: Path, arm: str, extra: list[str]
) -> dict[str, Any]:
    c = work / f"{name}.{arm}.c"
    s = work / f"{name}.{arm}.s"
    o = work / f"{name}.{arm}.o"
    c.write_text(src)
    base = [
        str(clang),
        "-target",
        "haydn-unknown-elf",
        "-O2",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-exceptions",
    ]
    for f in extra:
        base.extend(["-mllvm", f])

    cmd_s = base + ["-S", "-o", str(s), str(c)]
    r_s = subprocess.run(cmd_s, capture_output=True, text=True)
    row: dict[str, Any] = {
        "kernel": name,
        "arm": arm,
        "rc": r_s.returncode,
        "cmd": " ".join(cmd_s),
        "compile_ok": False,
        "obj_ok": False,
    }
    if r_s.returncode != 0:
        row["err"] = (r_s.stderr or r_s.stdout)[-1000:]
        return row

    asm_text = s.read_text(errors="replace")
    metrics = measure_asm(asm_text)
    row.update(metrics)
    row["asm_path"] = str(s)
    row["asm_sha256"] = hashlib.sha256(asm_text.encode()).hexdigest()
    row["compile_ok"] = metrics["emitted_cycles"] > 0

    # Ordinary-arm lifecycle STATS (asserts builds). Optional evidence only.
    if arm == "ordinary" and row["compile_ok"]:
        cmd_st = base + ["-S", "-mllvm", "-stats", "-o", "/dev/null", str(c)]
        try:
            r_st = subprocess.run(cmd_st, capture_output=True, text=True, timeout=60)
            stats_text = (r_st.stderr or "") + "\n" + (r_st.stdout or "")
            stats = parse_haydn_sched_stats(stats_text)
        except (OSError, subprocess.TimeoutExpired):
            stats = {}
        row["sched_stats"] = stats
        row["lifecycle"] = {
            "resource_admission_pin": stats_has_substr(
                stats, "post-ra-sched", "resource admission"
            ),
            "emitted_cycle_audit": stats_has_substr(
                stats, "post-ra-sched", "architectural cycles"
            ),
            "altdesc_clear": stats_has_substr(
                stats, "post-ra-sched", "alternate descriptors"
            ),
            "prera_phase_firewall": stats_has_substr(
                stats, "prera-sched", "phase-firewall"
            ),
            "stats_available": bool(stats),
        }

    size_bin = haydn_bin() / "llvm-size"
    cmd_o = base + ["-c", "-o", str(o), str(c)]
    r_o = subprocess.run(cmd_o, capture_output=True, text=True)
    row["obj_rc"] = r_o.returncode
    row["obj_cmd"] = " ".join(cmd_o)
    if r_o.returncode != 0:
        row["obj_err"] = (r_o.stderr or r_o.stdout)[-800:]
    else:
        ob = measure_object_text_bytes(o, size_bin)
        row.update(ob)
        if ob.get("obj_ok") and metrics["emitted_cycles"] > 0:
            est = metrics["text_bytes_est"]
            tb = ob.get("text_bytes_obj")
            row["text_bytes_est_matches_obj"] = tb == est
            row["p8_cycles_match_obj"] = (
                ob.get("p8_parcel_count_obj") == metrics["emitted_cycles"]
            )
    return row


def delta(o: dict[str, Any], p: dict[str, Any]) -> dict[str, Any]:
    if not o.get("compile_ok") or not p.get("compile_ok"):
        return {"comparable": False}
    d: dict[str, Any] = {"comparable": True}
    for k in (
        "emitted_cycles",
        "real_ops",
        "nops",
        "enc_fill",
        "occupancy",
        "text_bytes_est",
        "text_bytes_obj",
        "spill_store_lines",
        "spill_load_lines",
        "swps_comment_lines",
    ):
        if isinstance(o.get(k), (int, float)) and isinstance(p.get(k), (int, float)):
            d[f"delta_{k}"] = (
                round(p[k] - o[k], 4) if isinstance(p[k], float) else (p[k] - o[k])
            )
    d["product_swps_stagecount1"] = p.get("swps_stagecount1", 0)
    d["product_swps_multistage"] = p.get("swps_multistage", 0)
    d["containment_multistage_absent"] = p.get("swps_multistage", 0) == 0
    return d


def git_rev(path: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except Exception:
        return "unknown"


def parse_haydn_sched_stats(text: str) -> dict[str, int]:
    """Parse release-visible ordinary lifecycle STATS from llc/clang -stats."""
    out: dict[str, int] = {}
    for line in (text or "").splitlines():
        m = RE_STAT_LINE.match(line)
        if not m:
            continue
        count, pass_name, desc = int(m.group(1)), m.group(2), m.group(3).strip()
        key = f"{pass_name}:{desc}"
        out[key] = out.get(key, 0) + count
    return out


def stats_has_substr(stats: dict[str, int], pass_name: str, needle: str) -> bool:
    for k, v in stats.items():
        if not k.startswith(pass_name + ":"):
            continue
        if needle in k and v > 0:
            return True
    return False


def resource_surface_evidence() -> dict[str, Any]:
    """Shared availability-aware aggregate surface (not competitive invent)."""
    return {
        "availability_aware_resource_record": True,
        "aggregate_resource_surface_only": True,
        "per_op_resource_records_admitted": PER_OP_RESOURCE_RECORDS_ADMITTED,
        "complete_model": 0,
        "competitive_ii_density_claims": COMPETITIVE_II_DENSITY_CLAIMS,
        "aggregate": dict(AGGREGATE_RESOURCE_SURFACE),
        "per_op_availability_samples": [dict(x) for x in PER_OP_AVAILABILITY_SAMPLES],
        "sequentialization_is_not_legality": True,
        "product_coissue_probe_is_legality": True,
    }



def product_summary_polarity() -> dict[str, Any]:
    return {
        "product_sms_containment_max_stage_count": PRODUCT_SMS_CONTAINMENT_MAX_STAGE_COUNT,
        "per_op_resource_records_admitted": PER_OP_RESOURCE_RECORDS_ADMITTED,
        "competitive_ii_density_claims": COMPETITIVE_II_DENSITY_CLAIMS,
        "structural_capability_only": True,
        "complete_model": 0,
        "availability_aware_resource_record": True,
        "aggregate_resource_surface_only": True,
        "ordinary_list_schedule_commit_baseline_required": True,
        "p8_object_text_bytes_required": True,
        "golden_hash_binding_required": True,
        "semantic_host_execution_required_when_available": True,
        "resource_surface": resource_surface_evidence(),
        "sequentialization_is_not_legality": True,
        "product_coissue_probe_is_legality": True,
        "m18_unclaimable": M18_UNCLAIMABLE,
        "nat_ipc_measured_miss": NAT_IPC_MEASURED_MISS,
        "swps_asm_measured_miss": SWPS_ASM_MEASURED_MISS,
        "m_eval_only": dict(M_EVAL_ONLY),
        "sf1_sf3_gates_closed": SF1_SF3_GATES_CLOSED,
        "stage0_ib_pp_revive": STAGE0_IB_PP_REVIVE,
        "three_arm": THREE_ARM,
        "object_mc_identity_only": True,
        "ipc_proxy": dict(IPC_PROXY_MEASURED_MISS),
    }


def find_run_c() -> Path | None:
    env = os.environ.get("BUNDLESIM_RUN_C")
    candidates = []
    if env:
        candidates.append(Path(env))
    root = os.environ.get("BUNDLESIM_ROOT")
    if root:
        candidates.extend(
            [
                Path(root) / "build" / "run_c",
                Path(root) / "build" / "bundlesim" / "run_c",
            ]
        )
    candidates.extend(
        [
            Path("/ssd2/mhyang/BundleSim/build/run_c"),
            Path("/ssd2/mhyang/BundleSim/build/bundlesim/run_c"),
        ]
    )
    for p in candidates:
        if p.is_file() and os.access(p, os.X_OK):
            return p
    return None


def find_record_artifact_script() -> Path | None:
    candidates = [
        Path(os.environ["BUNDLESIM_ROOT"]) / "scripts" / "record_haydn_artifact_set.py"
        if os.environ.get("BUNDLESIM_ROOT")
        else None,
        Path("/ssd2/mhyang/BundleSim/scripts/record_haydn_artifact_set.py"),
    ]
    return next((p for p in candidates if p is not None and p.is_file()), None)


def load_live_artifact(hb: Path) -> dict[str, Any] | None:
    candidates = [
        hb.parent / "sysroot" / "haydn-unknown-elf" / "ARTIFACT.json",
        Path("/ssd2/mhyang/haydn-build/sysroot/haydn-unknown-elf/ARTIFACT.json"),
    ]
    env_sr = os.environ.get("BUNDLESIM_SYSROOT")
    if env_sr:
        candidates.insert(0, Path(env_sr) / "ARTIFACT.json")
        candidates.insert(0, Path(env_sr))
    for p in candidates:
        path = p if p.name == "ARTIFACT.json" else p / "ARTIFACT.json"
        if not path.is_file():
            continue
        try:
            data = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError):
            continue
        if isinstance(data, dict) and data.get("artifact_id"):
            data["_artifact_path"] = str(path)
            return data
    return None


def extract_golden_binding(art: dict[str, Any] | None) -> dict[str, Any]:
    if not art:
        return {
            "artifact_id": None,
            "golden_complete": False,
            "golden_hashes": {n: None for n in GOLDEN_FILES},
            "binding_present": False,
            "source": None,
        }
    golden = art.get("golden") or {}
    files = golden.get("files") or {}
    hashes: dict[str, Any] = {}
    for name in GOLDEN_FILES:
        entry = files.get(name) or {}
        hashes[name] = entry.get("sha256")
    complete = bool(golden.get("complete")) and all(
        isinstance(hashes[n], str) and len(hashes[n]) == 64 for n in GOLDEN_FILES
    )
    return {
        "artifact_id": art.get("artifact_id"),
        "golden_complete": complete,
        "golden_hashes": hashes,
        "binding_present": complete and bool(art.get("artifact_id")),
        "source": art.get("_artifact_path") or art.get("schema") or "artifact",
        "schema": art.get("schema"),
    }


def record_artifact() -> dict[str, Any] | None:
    rec = find_record_artifact_script()
    if rec is None:
        return {"error": "record_haydn_artifact_set.py not found"}
    env = os.environ.copy()
    env.setdefault("HAYDN_BIN", str(haydn_bin()))
    try:
        r = subprocess.run(
            [sys.executable, str(rec), "--pretty"],
            capture_output=True,
            text=True,
            env=env,
            timeout=120,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"error": str(exc)}
    if r.returncode != 0:
        return {"error": (r.stderr or r.stdout)[-800:], "rc": r.returncode}
    try:
        return json.loads(r.stdout)
    except json.JSONDecodeError:
        return {
            "error": "artifact JSON parse failed",
            "raw": (r.stdout or "")[-400:],
        }


def seat_golden_binding(
    name: str,
    ordinary: dict[str, Any],
    product: dict[str, Any],
    golden: dict[str, Any],
) -> dict[str, Any]:
    o_hash = ordinary.get("obj_sha256")
    p_hash = product.get("obj_sha256")
    binding_ok = bool(
        golden.get("binding_present")
        and isinstance(o_hash, str)
        and len(o_hash) == 64
        and isinstance(p_hash, str)
        and len(p_hash) == 64
    )
    return {
        "kernel": name,
        "artifact_id": golden.get("artifact_id"),
        "golden_complete": golden.get("golden_complete"),
        "golden_hashes": golden.get("golden_hashes"),
        "ordinary_obj_sha256": o_hash,
        "product_obj_sha256": p_hash,
        "ordinary_asm_sha256": ordinary.get("asm_sha256"),
        "product_asm_sha256": product.get("asm_sha256"),
        "binding_ok": binding_ok,
    }


def run_semantic_host(
    name: str, work: Path, arm: str, extra_mllvm: list[str]
) -> dict[str, Any]:
    run_c = find_run_c()
    out: dict[str, Any] = {
        "kernel": name,
        "arm": arm,
        "semantic_ok": False,
        "host_available": run_c is not None,
    }
    if run_c is None:
        out["skipped"] = True
        out["reason"] = "run_c not found"
        return out
    if name not in SEMANTIC_DRIVERS:
        out["skipped"] = True
        out["reason"] = f"no semantic driver for {name}"
        return out

    sem_work = work / f"sem-{arm}-{name}"
    sem_work.mkdir(parents=True, exist_ok=True)
    cpath = sem_work / f"{name}.{arm}.sem.c"
    cpath.write_text(SEMANTIC_DRIVERS[name])

    cmd: list[str] = [str(run_c), str(cpath), "-O2", "--keep"]
    for flag in extra_mllvm:
        cmd.extend(["--cflag", "-mllvm", "--cflag", flag])

    env = os.environ.copy()
    hb = str(haydn_bin())
    env.setdefault("HAYDN_BIN", hb)
    env.setdefault("BUNDLESIM_HAYDN_TOOLCHAIN_BIN", hb)
    env["BUNDLESIM_WORK"] = str(sem_work)

    try:
        r = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            env=env,
            timeout=180,
            cwd=str(sem_work),
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        out["err"] = str(exc)
        return out

    text = (r.stdout or "") + "\n" + (r.stderr or "")
    out["rc"] = r.returncode
    out["cmd"] = " ".join(cmd)
    out["stdout_tail"] = text[-600:]

    m = RE_RUN_C_STOP.search(text)
    if m:
        out["stop_reason"] = m.group(1)
        out["guest_exit_code"] = int(m.group(2))
        out["committed_bundles"] = int(m.group(3))
    wm = RE_WORK_KEPT.search(text)
    if wm:
        kept = Path(wm.group(1))
        out["run_c_work"] = str(kept)
        rj = kept / "result.json"
        if rj.is_file():
            try:
                res = json.loads(rj.read_text())
                out["result_json"] = {
                    "stop_reason": res.get("stop_reason"),
                    "guest_exit_code": res.get("guest_exit_code"),
                    "committed_bundles": res.get("committed_bundles"),
                    "status": res.get("status"),
                    "elf_sha256": res.get("elf_sha256"),
                    "qualifying": res.get("qualifying"),
                }
                out["stop_reason"] = res.get("stop_reason", out.get("stop_reason"))
                out["guest_exit_code"] = res.get(
                    "guest_exit_code", out.get("guest_exit_code")
                )
                out["committed_bundles"] = res.get(
                    "committed_bundles", out.get("committed_bundles")
                )
                out["elf_sha256"] = res.get("elf_sha256")
            except (OSError, json.JSONDecodeError) as exc:
                out["result_json_err"] = str(exc)

    out["semantic_ok"] = (
        out.get("stop_reason") == "GUEST_EXIT"
        and out.get("guest_exit_code") == 0
        and isinstance(out.get("committed_bundles"), int)
        and out["committed_bundles"] > 0
    )
    return out


def cmd_self_test(_args: Any = None) -> int:
    errs: list[str] = []
    if len(CANARIES) != 12:
        errs.append(f"corpus size {len(CANARIES)} != 12")
    if tuple(CANARIES.keys()) != REQUIRED_CORPUS:
        errs.append("REQUIRED_CORPUS must match CANARIES key order")
    if tuple(SEMANTIC_DRIVERS.keys()) != REQUIRED_CORPUS:
        errs.append("SEMANTIC_DRIVERS must cover REQUIRED_CORPUS in order")
    for name in REQUIRED_CORPUS:
        if name not in CANARIES or not CANARIES[name].strip():
            errs.append(f"missing canary body: {name}")
        if name not in SEMANTIC_DRIVERS or "int main" not in SEMANTIC_DRIVERS[name]:
            errs.append(f"missing semantic driver main: {name}")
    m = measure_asm("foo:\n  { add32 r1, r2, r3 }\n  { nop; nop; nop }\n")
    if m["emitted_cycles"] != 2 or m["real_ops"] != 1 or m["nops"] != 3:
        errs.append(f"measure_asm sample unexpected: {m}")
    empty = measure_asm("empty_fn:\n  .size empty_fn, 0\n")
    if empty["emitted_cycles"] != 0:
        errs.append(f"empty asm must not invent parcels: {empty}")
    leak = measure_asm(
        "bar:\n  // #<swps> stages=2 ii=3\n  { xor32 r0, r0, r0 }\n"
    )
    if leak["swps_multistage"] != 1 or leak["swps_stagecount1"] != 0:
        errs.append(f"multistage SWPS parse unexpected: {leak}")
    sc1 = measure_asm(
        "baz:\n  // #<swps> stages=1 ii=1\n  { add32 r1, r1, r1 }\n"
    )
    if sc1["swps_stagecount1"] != 1 or sc1["swps_multistage"] != 0:
        errs.append(f"stagecount1 SWPS parse unexpected: {sc1}")
    if (24 % PARCEL_BYTES) != 0:
        errs.append("PARCEL_BYTES geometry broken")
    pol = product_summary_polarity()
    for k, v in (
        ("per_op_resource_records_admitted", False),
        ("competitive_ii_density_claims", False),
        ("product_sms_containment_max_stage_count", 1),
        ("complete_model", 0),
        ("structural_capability_only", True),
        ("availability_aware_resource_record", True),
        ("aggregate_resource_surface_only", True),
        ("ordinary_list_schedule_commit_baseline_required", True),
        ("p8_object_text_bytes_required", True),
        ("golden_hash_binding_required", True),
        ("semantic_host_execution_required_when_available", True),
        ("sequentialization_is_not_legality", True),
        ("product_coissue_probe_is_legality", True),
        ("m18_unclaimable", True),
        ("nat_ipc_measured_miss", True),
        ("swps_asm_measured_miss", True),
        ("sf1_sf3_gates_closed", False),
        ("stage0_ib_pp_revive", False),
        ("three_arm", THREE_ARM),
        ("object_mc_identity_only", True),
    ):
        if pol[k] != v:
            errs.append(f"{k} must be {v}")
    ipc = pol.get("ipc_proxy") or {}
    if ipc.get("measured_miss") is not True or ipc.get("competitive_claim"):
        errs.append(f"ipc_proxy must stay a measured miss: {ipc}")
    if ipc.get("same_artifact") is not True:
        errs.append(f"ipc_proxy must be same-artifact: {ipc}")
    mev = pol.get("m_eval_only") or {}
    for mid in ("M2", "M17", "M18", "M19", "M20", "M21", "M22"):
        if mev.get(mid) is not True:
            errs.append(f"{mid} must stay eval-only: {mev}")
    surf = pol.get("resource_surface") or {}
    agg = surf.get("aggregate") or {}
    if agg.get("gpr_write_ports") != 2 or agg.get("issue_width") != 3:
        errs.append(f"aggregate surface polarity unexpected: {agg}")
    if agg.get("num_exec_units") != 7 or agg.get("load_latency_scaffold") != 2:
        errs.append(f"aggregate capacity polarity unexpected: {agg}")
    samples = surf.get("per_op_availability_samples") or []
    if len(samples) < 4:
        errs.append("per_op_availability_samples underfilled")
    for s in samples:
        if s.get("availability") != "AggregateCeilingsOnly":
            errs.append(f"sample must stay AggregateCeilingsOnly: {s}")
        if s.get("competitive_claims_allowed"):
            errs.append(f"sample competitive claim opened early: {s}")
    alone = {s["opcode"]: s.get("issues_alone_in_cycle") for s in samples}
    if alone.get("ARCTAN") is not True or alone.get("ADD32") is not False:
        errs.append(f"alone-in-cycle sample polarity unexpected: {alone}")
    sample_stats = parse_haydn_sched_stats(
        "  3 haydn-post-ra-sched - fail-closed per-op resource admission "
        "(product closed until golden import)\n"
        "  2 haydn-post-ra-sched - Number of architectural cycles reconstructed "
        "by post-RA leaveMBB\n"
        "  1 haydn-prera-sched  - pre-RA regions phase-firewall-checked for "
        "BUNDLE invent\n"
    )
    if not stats_has_substr(sample_stats, "post-ra-sched", "resource admission"):
        errs.append(f"stats parser missed resource admission: {sample_stats}")
    if not stats_has_substr(sample_stats, "post-ra-sched", "architectural cycles"):
        errs.append(f"stats parser missed cycle audit: {sample_stats}")
    o = {
        "compile_ok": True,
        "emitted_cycles": 4,
        "real_ops": 3,
        "nops": 1,
        "enc_fill": 0.75,
        "occupancy": 0.75,
        "text_bytes_est": 48,
        "text_bytes_obj": 48,
        "spill_store_lines": 0,
        "spill_load_lines": 0,
        "swps_comment_lines": 0,
        "swps_stagecount1": 0,
        "swps_multistage": 0,
        "obj_sha256": "a" * 64,
        "asm_sha256": "b" * 64,
    }
    p_arm = dict(o)
    p_arm["emitted_cycles"] = 3
    p_arm["text_bytes_est"] = 36
    p_arm["text_bytes_obj"] = 36
    p_arm["swps_stagecount1"] = 1
    p_arm["obj_sha256"] = "c" * 64
    d = delta(o, p_arm)
    if not d.get("comparable") or d.get("delta_emitted_cycles") != -1:
        errs.append(f"delta polarity unexpected: {d}")
    if not d.get("containment_multistage_absent"):
        errs.append("containment_multistage_absent must hold")
    fake_golden = {
        "binding_present": True,
        "artifact_id": "deadbeef",
        "golden_complete": True,
        "golden_hashes": {n: "d" * 64 for n in GOLDEN_FILES},
    }
    seat = seat_golden_binding("mac_loop", o, p_arm, fake_golden)
    if not seat.get("binding_ok"):
        errs.append(f"seat_golden_binding must accept complete hashes: {seat}")
    incomplete = seat_golden_binding(
        "mac_loop", o, p_arm, {**fake_golden, "binding_present": False}
    )
    if incomplete.get("binding_ok"):
        errs.append("incomplete golden must not bind")
    if len(GOLDEN_FILES) != 5:
        errs.append("GOLDEN_FILES must be five STATUS authorities")
    if errs:
        print("SELF-TEST FAIL:", file=sys.stderr)
        for e in errs:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print(
        "SELF-TEST OK freestanding-12 per_op_admitted=false "
        "competitive_claims=false stagecount1_containment=1 complete_model=0 "
        "availability_aware=true aggregate_only=true "
        "ordinary_baseline_required=true p8_object_required=true "
        "resource_surface=bound sequentialize_not_legality=true "
        "golden_hash_binding_required=true semantic_host_required_when_available=true "
        f"three_arm={THREE_ARM} m18_unclaimable=true nat_ipc_measured_miss=true "
        "swps_asm_measured_miss=true m_eval_only=M2,M17-M22 "
        "sf1_sf3_closed=false object_mc_identity_only=true "
        "ipc_proxy_measured_miss=true "
        f"semantic_drivers={len(SEMANTIC_DRIVERS)}"
    )
    return 0


def cmd_collect(args: argparse.Namespace) -> int:
    hb = haydn_bin()
    clang = hb / "clang"
    if not clang.is_file():
        print(f"error: clang not found at {clang}", file=sys.stderr)
        return 2
    names = (
        [k.strip() for k in args.kernels.split(",") if k.strip()]
        if args.kernels
        else list(CANARIES)
    )
    for n in names:
        if n not in CANARIES:
            print(f"error: unknown kernel {n!r}", file=sys.stderr)
            return 2
    work = (
        Path(args.work)
        if args.work
        else Path(tempfile.mkdtemp(prefix="sched-artifact-"))
    )
    work.mkdir(parents=True, exist_ok=True)

    live_art = load_live_artifact(hb)
    recorded_art = None
    if getattr(args, "with_artifact", False):
        recorded_art = record_artifact()
        if recorded_art and recorded_art.get("error") and not live_art:
            print(
                f"warning: artifact record failed: {recorded_art.get('error')}",
                file=sys.stderr,
            )
    art = None
    if recorded_art and not recorded_art.get("error") and recorded_art.get("artifact_id"):
        art = recorded_art
        art["_artifact_path"] = "record_haydn_artifact_set.py"
    elif live_art:
        art = live_art
    golden = extract_golden_binding(art)

    run_semantic = not getattr(args, "no_semantic", False)
    run_c_path = find_run_c()
    semantic_available = run_c_path is not None

    kernels = []
    for name in names:
        o = compile_kernel(
            name,
            CANARIES[name],
            work,
            clang,
            "ordinary",
            ["-enable-pipeliner=0"],
        )
        p = compile_kernel(name, CANARIES[name], work, clang, "product", [])
        seat = seat_golden_binding(name, o, p, golden)
        entry: dict[str, Any] = {
            "kernel": name,
            "ordinary": o,
            "product": p,
            "stagecount1_contribution": delta(o, p),
            "seat_binding": seat,
        }
        if run_semantic and semantic_available:
            entry["semantic"] = {
                "ordinary": run_semantic_host(
                    name, work, "ordinary", ["-enable-pipeliner=0"]
                ),
                "product": run_semantic_host(name, work, "product", []),
            }
            entry["semantic_ok"] = bool(
                entry["semantic"]["ordinary"].get("semantic_ok")
                and entry["semantic"]["product"].get("semantic_ok")
            )
        else:
            entry["semantic"] = {
                "skipped": True,
                "reason": (
                    "disabled by --no-semantic"
                    if not run_semantic
                    else "run_c not found"
                ),
            }
            entry["semantic_ok"] = False
        kernels.append(entry)

    ok_o = sum(1 for k in kernels if k["ordinary"].get("compile_ok"))
    ok_p = sum(1 for k in kernels if k["product"].get("compile_ok"))
    leaks = sum(
        1 for k in kernels if (k["product"].get("swps_multistage") or 0) > 0
    )
    obj_ok_o = sum(1 for k in kernels if k["ordinary"].get("obj_ok"))
    obj_ok_p = sum(1 for k in kernels if k["product"].get("obj_ok"))
    p8_ok = all(
        (k[arm].get("p8_parcel_aligned") is True)
        for k in kernels
        for arm in ("ordinary", "product")
        if k[arm].get("obj_ok")
    )
    p8_match = all(
        (k[arm].get("p8_cycles_match_obj") is True)
        for k in kernels
        for arm in ("ordinary", "product")
        if k[arm].get("obj_ok") and k[arm].get("compile_ok")
    )

    def mean(arm: str, key: str) -> float:
        vals = [
            k[arm].get(key, 0.0)
            for k in kernels
            if k[arm].get("compile_ok")
            and isinstance(k[arm].get(key), (int, float))
        ]
        return round(sum(vals) / len(vals), 4) if vals else 0.0

    ord_baseline = ok_o == len(names) and all(
        (k["ordinary"].get("emitted_cycles") or 0) > 0
        for k in kernels
        if k["ordinary"].get("compile_ok")
    )
    obj_baseline = (
        obj_ok_o == len(names)
        and obj_ok_p == len(names)
        and p8_ok
        and p8_match
    )

    life_keys = (
        "resource_admission_pin",
        "emitted_cycle_audit",
        "altdesc_clear",
        "prera_phase_firewall",
        "stats_available",
    )
    life_counts = {k: 0 for k in life_keys}
    life_n = 0
    for k in kernels:
        life = (k.get("ordinary") or {}).get("lifecycle") or {}
        if not life:
            continue
        life_n += 1
        for key in life_keys:
            if life.get(key):
                life_counts[key] += 1
    ordinary_lifecycle = {
        "kernels_with_stats": life_n,
        "resource_admission_pin_kernels": life_counts["resource_admission_pin"],
        "emitted_cycle_audit_kernels": life_counts["emitted_cycle_audit"],
        "altdesc_clear_kernels": life_counts["altdesc_clear"],
        "prera_phase_firewall_kernels": life_counts["prera_phase_firewall"],
        "stats_available_kernels": life_counts["stats_available"],
        "ordinary_lifecycle_stats_qualified": (
            life_n == len(names)
            and life_counts["resource_admission_pin"] == len(names)
            and life_counts["emitted_cycle_audit"] == len(names)
            and life_counts["prera_phase_firewall"] == len(names)
        ),
    }

    golden_all = (
        golden.get("binding_present") is True
        and all((k.get("seat_binding") or {}).get("binding_ok") for k in kernels)
    )
    semantic_ran = run_semantic and semantic_available
    semantic_all = semantic_ran and all(k.get("semantic_ok") for k in kernels)
    semantic_ok_n = sum(1 for k in kernels if k.get("semantic_ok"))

    summary: dict[str, Any] = {
        "schema": "haydn-sched-artifact-measure-v3",
        "source": "llvm/utils/haydn/measure_sched_artifact.py",
        "toolchain": str(hb),
        "compiler_identity": compiler_identity(hb),
        "work": str(work),
        "llvm_rev": git_rev(MONOREPO),
        "corpus": "freestanding-12",
        "declared_kernels": list(names),
        "n_kernels": len(kernels),
        "ordinary_compile_ok": ok_o,
        "product_compile_ok": ok_p,
        "ordinary_obj_ok": obj_ok_o,
        "product_obj_ok": obj_ok_p,
        "ordinary_mean_emitted_cycles": mean("ordinary", "emitted_cycles"),
        "product_mean_emitted_cycles": mean("product", "emitted_cycles"),
        "ordinary_mean_enc_fill": mean("ordinary", "enc_fill"),
        "product_mean_enc_fill": mean("product", "enc_fill"),
        "ordinary_mean_occupancy": mean("ordinary", "occupancy"),
        "product_mean_occupancy": mean("product", "occupancy"),
        "ordinary_mean_text_bytes_obj": mean("ordinary", "text_bytes_obj"),
        "product_mean_text_bytes_obj": mean("product", "text_bytes_obj"),
        "product_multistage_swps_leaks": leaks,
        "sms_stagecount1_containment_ok": leaks == 0,
        "ordinary_list_schedule_commit_baseline": ord_baseline,
        "p8_object_text_baseline": obj_baseline,
        "p8_parcel_aligned_all": p8_ok,
        "p8_cycles_match_obj_all": p8_match,
        "ordinary_lifecycle": ordinary_lifecycle,
        "golden_binding": golden,
        "golden_hash_binding_all_seats": golden_all,
        "semantic_host_available": semantic_available,
        "semantic_host_ran": semantic_ran,
        "semantic_host_ok": semantic_ok_n,
        "semantic_host_all_seats": semantic_all,
        "run_c_path": str(run_c_path) if run_c_path else None,
        "notes": [
            "Full freestanding-12 corpus; ordinary list-schedule/commit baseline; "
            "object .text P8 parcel geometry; golden-hash binding per seat; "
            "semantic host via BundleSim run_c when present; availability-aware "
            "aggregate resource surface + optional ordinary lifecycle STATS; "
            "sequentialize is recovery-only; no peer thresholds; competitive "
            "claims closed until admission.",
        ],
        "kernels": kernels,
    }
    summary.update(product_summary_polarity())
    if art is not None:
        summary["artifact"] = {
            "artifact_id": art.get("artifact_id"),
            "golden_complete": (art.get("golden") or {}).get("complete"),
            "source": art.get("_artifact_path"),
            "raw_error": art.get("error"),
        }

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(summary, indent=2) + "\n")
    print(
        f"wrote {out} kernels={len(kernels)} ord_ok={ok_o} prod_ok={ok_p} "
        f"multistage_leaks={leaks} ordinary_baseline={int(ord_baseline)} "
        f"obj_ok_o={obj_ok_o} obj_ok_p={obj_ok_p} p8_baseline={int(obj_baseline)} "
        f"golden_all={int(golden_all)} semantic_ok={semantic_ok_n}/{len(kernels)} "
        f"semantic_all={int(semantic_all)}"
    )

    structural_ok = (
        ok_o == len(names)
        and ok_p == len(names)
        and leaks == 0
        and ord_baseline
        and obj_baseline
    )
    golden_required = golden.get("binding_present") is True or art is not None
    if getattr(args, "allow_missing_golden", False):
        golden_required = False
    golden_ok = (not golden_required) or golden_all

    semantic_required = semantic_available and run_semantic
    if getattr(args, "allow_missing_semantic", False):
        semantic_required = False
    semantic_ok = (not semantic_required) or semantic_all

    ok = structural_ok and golden_ok and semantic_ok
    return 0 if ok else 1


def cmd_report(args: argparse.Namespace) -> int:
    data = json.loads(Path(args.json).read_text())
    print(f"schema: {data.get('schema')}")
    print(f"source: {data.get('source')}")
    print(
        f"kernels: {data.get('n_kernels')} ord_ok={data.get('ordinary_compile_ok')} "
        f"prod_ok={data.get('product_compile_ok')} "
        f"obj_ok_o={data.get('ordinary_obj_ok')} obj_ok_p={data.get('product_obj_ok')}"
    )
    print(f"ordinary_mean_emitted_cycles: {data.get('ordinary_mean_emitted_cycles')}")
    print(f"product_mean_emitted_cycles: {data.get('product_mean_emitted_cycles')}")
    print(f"ordinary_mean_text_bytes_obj: {data.get('ordinary_mean_text_bytes_obj')}")
    print(f"product_mean_text_bytes_obj: {data.get('product_mean_text_bytes_obj')}")
    print(f"sms_stagecount1_containment_ok: {data.get('sms_stagecount1_containment_ok')}")
    print(
        f"ordinary_list_schedule_commit_baseline: "
        f"{data.get('ordinary_list_schedule_commit_baseline')}"
    )
    print(f"p8_object_text_baseline: {data.get('p8_object_text_baseline')}")
    print(
        f"golden_hash_binding_all_seats: {data.get('golden_hash_binding_all_seats')}"
    )
    print(
        f"semantic_host_all_seats: {data.get('semantic_host_all_seats')} "
        f"(ok={data.get('semantic_host_ok')}/{data.get('n_kernels')} "
        f"available={data.get('semantic_host_available')})"
    )
    print(
        f"per_op_resource_records_admitted: "
        f"{data.get('per_op_resource_records_admitted')}"
    )
    print(f"complete_model: {data.get('complete_model')}")
    print(
        f"availability_aware_resource_record: "
        f"{data.get('availability_aware_resource_record')}"
    )
    print(
        f"aggregate_resource_surface_only: "
        f"{data.get('aggregate_resource_surface_only')}"
    )
    print(
        f"sequentialization_is_not_legality: "
        f"{data.get('sequentialization_is_not_legality')}"
    )
    print(
        f"product_coissue_probe_is_legality: "
        f"{data.get('product_coissue_probe_is_legality')}"
    )
    surf = data.get("resource_surface") or {}
    agg = surf.get("aggregate") or {}
    if agg:
        print(
            f"resource_surface.aggregate: gpr={agg.get('gpr_read_ports')}R"
            f"{agg.get('gpr_write_ports')}W issue={agg.get('issue_width')} "
            f"units={agg.get('num_exec_units')}"
        )
    samples = surf.get("per_op_availability_samples") or []
    if samples:
        print(
            "per_op_availability_samples: "
            + ", ".join(
                f"{s.get('opcode')}={s.get('availability')}"
                f"{'/alone' if s.get('issues_alone_in_cycle') else ''}"
                for s in samples
            )
        )
    life = data.get("ordinary_lifecycle") or {}
    if life:
        print(
            f"ordinary_lifecycle_stats_qualified: "
            f"{life.get('ordinary_lifecycle_stats_qualified')} "
            f"(admit={life.get('resource_admission_pin_kernels')}/"
            f"audit={life.get('emitted_cycle_audit_kernels')}/"
            f"fw={life.get('prera_phase_firewall_kernels')}/"
            f"n={life.get('kernels_with_stats')})"
        )
    print(
        f"product_sms_containment_max_stage_count: "
        f"{data.get('product_sms_containment_max_stage_count')}"
    )
    ci = data.get("compiler_identity") or {}
    if ci:
        print(f"compiler: {ci.get('clang_version')}")
        print(f"clang_sha256: {ci.get('clang_sha256')}")
    art = data.get("artifact") or {}
    if art:
        print(f"artifact_id: {art.get('artifact_id')}")
    gb = data.get("golden_binding") or {}
    if gb:
        print(
            f"golden_complete: {gb.get('golden_complete')} "
            f"artifact_id={gb.get('artifact_id')}"
        )
    for k in data.get("kernels", []):
        o, p, d = (
            k.get("ordinary", {}),
            k.get("product", {}),
            k.get("stagecount1_contribution", {}),
        )
        seat = k.get("seat_binding") or {}
        print(
            f"  {k.get('kernel')}: ord_cyc={o.get('emitted_cycles')} "
            f"prod_cyc={p.get('emitted_cycles')} "
            f"ord_txt={o.get('text_bytes_obj')} prod_txt={p.get('text_bytes_obj')} "
            f"delta_cyc={d.get('delta_emitted_cycles')} "
            f"ms_leak={p.get('swps_multistage', 0)} "
            f"p8={o.get('p8_parcel_aligned')}/{p.get('p8_parcel_aligned')} "
            f"bind={seat.get('binding_ok')} sem={k.get('semantic_ok')}"
        )
    return 0


def main() -> int:
    if len(sys.argv) >= 2 and sys.argv[1] in ("--self-test", "self-test"):
        return cmd_self_test()
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("collect")
    c.add_argument("--out", required=True)
    c.add_argument("--work")
    c.add_argument("--kernels")
    c.add_argument(
        "--with-artifact",
        action="store_true",
        help="restamp product artifact_id via BundleSim record_haydn_artifact_set.py",
    )
    c.add_argument(
        "--no-semantic",
        action="store_true",
        help="skip BundleSim run_c semantic host execution (not product default)",
    )
    c.add_argument(
        "--allow-missing-golden",
        action="store_true",
        help="do not fail when ARTIFACT golden binding is absent",
    )
    c.add_argument(
        "--allow-missing-semantic",
        action="store_true",
        help="do not fail when semantic host is available but a seat fails",
    )
    c.set_defaults(func=cmd_collect)
    r = sub.add_parser("report")
    r.add_argument("json")
    r.set_defaults(func=cmd_report)
    st = sub.add_parser("self-test")
    st.set_defaults(func=cmd_self_test)
    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
