#!/usr/bin/env bash
# Haydn per-commit / nightly CI gate for an air-gapped fork.
#
# There is no GitHub Actions workflow (llvm/.github is upstream; hard constraint
# #0 forbids editing it). This script is the local cron + runner. It wraps the
# existing product helpers and writes one machine-readable verdict JSON.
# It does not reimplement lit parsing, XFAIL ledger checks, sysroot rebuild,
# or BundleSim ctest.
#
# Sequence (call peers; do not reimplement):
#   1. flock $LLVM_BUILD/.ninja_lock ninja
#   2. A0: flock ninja HaydnTests HaydnFormatERecordsCheck; run HaydnTests
#   3. Haydn lit via BundleSim scripts/run_haydn_lit_gate.sh
#      (Failed/XPASS only via llvm/utils/haydn/parse_lit_summary.py;
#       never substring-match Failed inside Expectedly Failed)
#   4. llvm/utils/haydn/check_xfail_ledger.py
#   5. sysroot: BundleSim scripts/build_haydn_llvm_libc.sh
#               + scripts/install_haydn_sysroot.sh
#   6. BundleSim scripts/run_full_gate.sh with INCLUDE_YARPGEN=0
#      (ctest exclude yarpgen + CoreMark e2e + Dhrystone e2e)
#   7. llvm/utils/haydn/write_ci_verdict.py → $OUT/verdict.json
#
# Peer scripts (do not fork a second gate):
#   /ssd2/mhyang/BundleSim/scripts/run_full_gate.sh
#   /ssd2/mhyang/BundleSim/scripts/run_haydn_lit_gate.sh
#   /ssd2/mhyang/BundleSim/scripts/parse_haydn_lit_summary.py
#   /ssd2/mhyang/BundleSim/scripts/build_haydn_llvm_libc.sh
#   /ssd2/mhyang/BundleSim/scripts/install_haydn_sysroot.sh
#   llvm/utils/haydn/parse_lit_summary.py
#   llvm/utils/haydn/check_xfail_ledger.py
#   llvm/utils/haydn/check_runtime_artifact_seats.py
#   llvm/utils/haydn/run_abi_conformance_matrix.sh
#   llvm/utils/haydn/write_ci_verdict.py
#
# Crontab example (do NOT install automatically):
#   # Nightly 02:15 local. Host must already have the trees and a green ninja dir.
#   15 2 * * * LLVM_SRC=/ssd/mhyang/llvm/llvm-head \
#     LLVM_BUILD=/ssd2/mhyang/haydn-build \
#     HAYDN_BIN=/ssd2/mhyang/haydn-build/bin \
#     BUNDLESIM=/ssd2/mhyang/BundleSim \
#     /ssd/mhyang/llvm/llvm-head/llvm/utils/haydn/run_ci_gate.sh \
#     >>/var/log/haydn-ci-nightly.log 2>&1
#
# Per-commit (same env, after push to the air-gapped remote):
#   LLVM_SRC=... LLVM_BUILD=... HAYDN_BIN=... BUNDLESIM=... \
#     llvm/utils/haydn/run_ci_gate.sh
#
# Usage:
#   run_ci_gate.sh
#   run_ci_gate.sh --dry-run [PATH]   # validate verdict JSON only; no ninja
#
# Env:
#   LLVM_SRC           monorepo root (default: derived from this script)
#   LLVM_BUILD         ninja dir (default: /ssd2/mhyang/haydn-build)
#   HAYDN_BIN          toolchain bin (default: $LLVM_BUILD/bin)
#   BUNDLESIM          BundleSim *repo root* (default: /ssd2/mhyang/BundleSim)
#   BUNDLESIM_BUILD    BundleSim cmake dir (default: $BUNDLESIM/build)
#   HAYDN_LIBC_BUILD   llvm-libc out-of-tree build
#   OUT                artifact directory (default: $LLVM_BUILD/ci-verdict/<ts>)
#
# Exit:
#   0  overall PASS
#   1  overall FAIL
#   2  setup / usage error (no verdict, or --dry-run schema error)
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LLVM_SRC="${LLVM_SRC:-$(cd "$SCRIPT_DIR/../../.." && pwd)}"
LLVM_BUILD="${LLVM_BUILD:-${BUILD:-/ssd2/mhyang/haydn-build}}"
HAYDN_BIN="${HAYDN_BIN:-${BUNDLESIM_HAYDN_TOOLCHAIN_BIN:-$LLVM_BUILD/bin}}"
WRITER="$SCRIPT_DIR/write_ci_verdict.py"
LEDGER="$SCRIPT_DIR/check_xfail_ledger.py"
SEATS="$SCRIPT_DIR/check_runtime_artifact_seats.py"
ABI_MATRIX="$SCRIPT_DIR/run_abi_conformance_matrix.sh"
PARSE_LIT="$SCRIPT_DIR/parse_lit_summary.py"

usage() {
  sed -n '2,70p' "$0" | sed 's/^# \?//'
}

# --- --dry-run: schema only, never ninja / lit / BundleSim ---
if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi
if [[ "${1:-}" == "--dry-run" ]]; then
  shift
  target="${1:-}"
  if [[ -z "$target" ]]; then
    echo "run_ci_gate: --dry-run needs a verdict JSON path" >&2
    exit 2
  fi
  exec python3 "$WRITER" --dry-run "$target"
fi

# BundleSim repo root (task env BUNDLESIM=/ssd2/mhyang/BundleSim is a directory).
_bs="${BUNDLESIM:-/ssd2/mhyang/BundleSim}"
if [[ -d "$_bs" && ! -f "$_bs" ]]; then
  BUNDLESIM_ROOT="$(cd "$_bs" && pwd)"
else
  BUNDLESIM_ROOT="${BUNDLESIM_ROOT:-/ssd2/mhyang/BundleSim}"
fi
BUNDLESIM_BUILD="${BUNDLESIM_BUILD:-$BUNDLESIM_ROOT/build}"
HAYDN_LIBC_BUILD="${HAYDN_LIBC_BUILD:-$(cd "$HAYDN_BIN/../.." 2>/dev/null && pwd)/llvm-libc-haydn-build}"

RUN_LIT_GATE="$BUNDLESIM_ROOT/scripts/run_haydn_lit_gate.sh"
RUN_FULL_GATE="$BUNDLESIM_ROOT/scripts/run_full_gate.sh"
BUILD_LIBC="$BUNDLESIM_ROOT/scripts/build_haydn_llvm_libc.sh"
INSTALL_SYSROOT="$BUNDLESIM_ROOT/scripts/install_haydn_sysroot.sh"

for p in "$WRITER" "$LEDGER" "$SEATS" "$ABI_MATRIX" "$PARSE_LIT" \
         "$RUN_LIT_GATE" "$RUN_FULL_GATE" "$BUILD_LIBC" "$INSTALL_SYSROOT"; do
  if [[ ! -f "$p" ]]; then
    echo "run_ci_gate: missing peer $p" >&2
    exit 2
  fi
done
if [[ ! -x "$HAYDN_BIN/llvm-lit" ]]; then
  echo "run_ci_gate: need HAYDN_BIN with llvm-lit (got $HAYDN_BIN)" >&2
  exit 2
fi
if [[ ! -d "$LLVM_BUILD" ]]; then
  echo "run_ci_gate: missing LLVM_BUILD=$LLVM_BUILD" >&2
  exit 2
fi

ts="$(date -u +%Y%m%d_%H%M%S)"
if [[ -n "${OUT:-}" ]]; then
  OUT_DIR="$OUT"
else
  OUT_DIR="$LLVM_BUILD/ci-verdict/$ts"
fi
mkdir -p "$OUT_DIR"
SUMMARY="$OUT_DIR/SUMMARY.txt"
VERDICT="$OUT_DIR/verdict.json"
NINJA_LOCK="$LLVM_BUILD/.ninja_lock"

head_sha="$(git -C "$LLVM_SRC" rev-parse HEAD 2>/dev/null || echo unknown)"
# Rebound: 08-17 full-gate manifest 28700d57 is not an ancestor. Bind the
# verdict to an in-repo commit (9e5c878a is the current GOALS stamp).
REBIND_ANCESTOR="${HAYDN_REBIND_ANCESTOR:-9e5c878a03921a31f76b1c251954cd14d9fcfd72}"
STALE_FULL_GATE_COMMIT="${HAYDN_STALE_FULL_GATE_COMMIT:-28700d57366a35a7d04e8adfbdf782743ec847e0}"
if ! git -C "$LLVM_SRC" merge-base --is-ancestor "$REBIND_ANCESTOR" HEAD 2>/dev/null; then
  echo "run_ci_gate: $REBIND_ANCESTOR is not an ancestor of HEAD (cannot rebind)" >&2
  exit 2
fi
if git -C "$LLVM_SRC" merge-base --is-ancestor "$STALE_FULL_GATE_COMMIT" HEAD 2>/dev/null; then
  : # unexpected; keep going and let parse_lit_summary refuse the stamp
fi
if [[ "$head_sha" == 28700d57* ]]; then
  echo "run_ci_gate: refuse stale full-gate commit 28700d57 (not an ancestor)" >&2
  exit 2
fi

log() { printf '%s\n' "$*" | tee -a "$SUMMARY"; }

# Parse run_full_gate.sh SUMMARY lines. Missing / SKIP → false (no false-green).
phase_ok_from_summary() {
  local file="$1" label="$2"
  if [[ ! -f "$file" ]]; then
    echo false
    return
  fi
  if grep -E "^${label}: PASS" "$file" >/dev/null 2>&1; then
    echo true
  else
    echo false
  fi
}

log "Haydn CI gate  $ts"
log "  llvm_src=$LLVM_SRC"
log "  llvm_build=$LLVM_BUILD"
log "  haydn_bin=$HAYDN_BIN"
log "  bundlesim_root=$BUNDLESIM_ROOT"
log "  out=$OUT_DIR"
log "  head_sha=$head_sha"
log "  lit_parser=$PARSE_LIT (Failed/XPASS only)"
log ""

build_ok=false
units_ok=false
haydn_lit_log=""
xfail_ledger_ok=false
sysroot_ok=false
bundlesim_ok=false
coremark_ok=false
dhrystone_ok=false
full_gate_summary=""

# --- 1) full ninja (wait on lock; do not steal) ---
log "======== NINJA ========"
set +e
flock "$NINJA_LOCK" ninja -C "$LLVM_BUILD" >"$OUT_DIR/ninja.log" 2>&1
build_rc=$?
set -e
if [[ "$build_rc" -eq 0 ]]; then
  build_ok=true
  log "NINJA: PASS"
else
  log "NINJA: FAIL rc=$build_rc (see $OUT_DIR/ninja.log)"
fi
log ""

# --- 2) A0 units (same flock) ---
log "======== A0 UNITS ========"
if [[ "$build_ok" == true ]]; then
  set +e
  flock "$NINJA_LOCK" ninja -C "$LLVM_BUILD" HaydnTests HaydnFormatERecordsCheck \
    >"$OUT_DIR/a0.log" 2>&1
  a0_rc=$?
  set -e
  haydn_tests_bin="$LLVM_BUILD/unittests/Target/Haydn/HaydnTests"
  if [[ "$a0_rc" -eq 0 && -x "$haydn_tests_bin" ]]; then
    set +e
    "$haydn_tests_bin" >>"$OUT_DIR/a0.log" 2>&1
    a0_rc=$?
    set -e
  elif [[ "$a0_rc" -eq 0 ]]; then
    log "A0: FAIL HaydnTests binary missing at $haydn_tests_bin"
    a0_rc=1
  fi
  if [[ "$a0_rc" -eq 0 ]]; then
    units_ok=true
    log "A0: PASS"
  else
    log "A0: FAIL rc=$a0_rc (see $OUT_DIR/a0.log)"
  fi
else
  log "A0: SKIP (ninja failed)"
fi
log ""

# --- 3) Haydn lit (canonical BundleSim wrapper → parse_lit_summary) ---
log "======== HAYDN LIT ========"
if [[ "$build_ok" == true ]]; then
  lit_out="$OUT_DIR/haydn-lit"
  mkdir -p "$lit_out"
  set +e
  HAYDN_BIN="$HAYDN_BIN" LLVM_SRC="$LLVM_SRC" OUT="$lit_out" \
    bash "$RUN_LIT_GATE" -sv >"$OUT_DIR/haydn-lit-gate.log" 2>&1
  lit_rc=$?
  set -e
  if [[ -f "$lit_out/haydn-lit.log" ]]; then
    haydn_lit_log="$lit_out/haydn-lit.log"
  fi
  if [[ "$lit_rc" -eq 0 ]]; then
    log "HAYDN_LIT: PASS (Failed=0 XPASS=0)"
  else
    log "HAYDN_LIT: FAIL rc=$lit_rc (see $lit_out)"
  fi
else
  log "HAYDN_LIT: SKIP (ninja failed)"
fi
log ""

# --- 4) XFAIL ledger ---
log "======== XFAIL LEDGER ========"
set +e
python3 "$LEDGER" --llvm-src "$LLVM_SRC" >"$OUT_DIR/xfail-ledger.log" 2>&1
xfail_rc=$?
set -e
if [[ "$xfail_rc" -eq 0 ]]; then
  set +e
  python3 "$LEDGER" --inventory-pin --llvm-src "$LLVM_SRC" \
    >>"$OUT_DIR/xfail-ledger.log" 2>&1
  inv_rc=$?
  set -e
  if [[ "$inv_rc" -eq 0 ]]; then
    xfail_ledger_ok=true
    log "XFAIL_LEDGER: PASS (PIPE-20/DG0 inventory-only)"
  else
    log "XFAIL_LEDGER: FAIL inventory-pin rc=$inv_rc (see $OUT_DIR/xfail-ledger.log)"
  fi
else
  log "XFAIL_LEDGER: FAIL rc=$xfail_rc (see $OUT_DIR/xfail-ledger.log)"
fi
log ""

# --- 5) sysroot rebuild ---
log "======== SYSROOT ========"
if [[ "$build_ok" == true ]]; then
  set +e
  HAYDN_BIN="$HAYDN_BIN" BUILD_DIR="$HAYDN_LIBC_BUILD" LLVM_SRC="$LLVM_SRC" \
    bash "$BUILD_LIBC" >"$OUT_DIR/sysroot.log" 2>&1
  sys_rc=$?
  if [[ "$sys_rc" -eq 0 ]]; then
    HAYDN_BIN="$HAYDN_BIN" BUILD_DIR="$HAYDN_LIBC_BUILD" \
      LLVM_SRC="$LLVM_SRC" HAYDN_LLVM_SRC="$LLVM_SRC" \
      bash "$INSTALL_SYSROOT" >>"$OUT_DIR/sysroot.log" 2>&1
    sys_rc=$?
  fi
  set -e
  if [[ "$sys_rc" -eq 0 ]]; then
    sysroot_ok=true
    log "SYSROOT: PASS"
    set +e
    python3 "$SCRIPT_DIR/record_haydn_artifact_set.py" --attach-owned \
      --llvm-src "$LLVM_SRC" --haydn-bin "$HAYDN_BIN" \
      >>"$OUT_DIR/sysroot.log" 2>&1
    attach_rc=$?
    set -e
    if [[ "$attach_rc" -ne 0 ]]; then
      sysroot_ok=false
      log "SYSROOT: FAIL attach-owned rc=$attach_rc"
    fi
  else
    log "SYSROOT: FAIL rc=$sys_rc (see $OUT_DIR/sysroot.log)"
  fi
else
  log "SYSROOT: SKIP (ninja failed)"
fi
log ""

# --- 5b) same-artifact seats + T-ABI9 matrix (no CoreMark / no full gate) ---
log "======== ARTIFACT SEATS / ABI MATRIX ========"
if [[ "$sysroot_ok" == true ]]; then
  set +e
  python3 "$SEATS" --require-sysroot --require-consumer-install \
    --llvm-src "$LLVM_SRC" --haydn-bin "$HAYDN_BIN" \
    >"$OUT_DIR/artifact-seats.log" 2>&1
  seats_rc=$?
  bash "$ABI_MATRIX" "$OUT_DIR/abi-matrix" >"$OUT_DIR/abi-matrix.log" 2>&1
  abi_rc=$?
  set -e
  if [[ "$seats_rc" -ne 0 || "$abi_rc" -ne 0 ]]; then
    sysroot_ok=false
    log "ARTIFACT_SEATS: rc=$seats_rc ABI_MATRIX: rc=$abi_rc (identity unbound / matrix red)"
  else
    log "ARTIFACT_SEATS: PASS ABI_MATRIX: PASS"
  fi
else
  log "ARTIFACT_SEATS: SKIP (sysroot failed)"
fi
log ""

# --- 6) BundleSim full gate (INCLUDE_YARPGEN=0; do not invent a second gate) ---
log "======== BUNDLESIM FULL GATE ========"
if [[ "$build_ok" == true ]]; then
  fg_out="$OUT_DIR/full-gate"
  mkdir -p "$fg_out"
  # Inner script treats BUNDLESIM as the simulator *binary*. Leave it unset
  # when the env value is the repo root so it can discover $BUILD_DIR/BundleSim.
  set +e
  env -u BUNDLESIM \
    HAYDN_BIN="$HAYDN_BIN" \
    BUNDLESIM_HAYDN_TOOLCHAIN_BIN="$HAYDN_BIN" \
    LLVM_SRC="$LLVM_SRC" \
    BUILD_DIR="$BUNDLESIM_BUILD" \
    INCLUDE_YARPGEN=0 \
    OUT="$fg_out" \
    bash "$RUN_FULL_GATE" >"$OUT_DIR/full-gate-runner.log" 2>&1
  fg_rc=$?
  set -e
  if [[ -f "$fg_out/SUMMARY.txt" ]]; then
    full_gate_summary="$fg_out/SUMMARY.txt"
    bundlesim_ok="$(phase_ok_from_summary "$full_gate_summary" CTEST)"
    coremark_ok="$(phase_ok_from_summary "$full_gate_summary" COREMARK)"
    dhrystone_ok="$(phase_ok_from_summary "$full_gate_summary" DHRYSTONE)"
  fi
  log "FULL_GATE: rc=$fg_rc ctest=$bundlesim_ok coremark=$coremark_ok dhrystone=$dhrystone_ok"
else
  log "FULL_GATE: SKIP (ninja failed)"
fi
log ""

# --- 7) verdict artifact ---
log_paths_json="$(python3 -c 'import json,sys; print(json.dumps({
  "summary": sys.argv[1],
  "ninja": sys.argv[2],
  "a0": sys.argv[3],
  "haydn_lit": sys.argv[4],
  "xfail_ledger": sys.argv[5],
  "sysroot": sys.argv[6],
  "bundlesim": sys.argv[7],
  "verdict": sys.argv[8],
}))' \
  "$SUMMARY" \
  "$OUT_DIR/ninja.log" \
  "$OUT_DIR/a0.log" \
  "${haydn_lit_log:-$OUT_DIR/haydn-lit-gate.log}" \
  "$OUT_DIR/xfail-ledger.log" \
  "$OUT_DIR/sysroot.log" \
  "${full_gate_summary:-$OUT_DIR/full-gate-runner.log}" \
  "$VERDICT")"

writer_args=(
  --out "$VERDICT"
  --head-sha "$head_sha"
  --build-ok "$build_ok"
  --units-ok "$units_ok"
  --xfail-ledger-ok "$xfail_ledger_ok"
  --sysroot-ok "$sysroot_ok"
  --bundlesim-regression-ok "$bundlesim_ok"
  --coremark-ok "$coremark_ok"
  --dhrystone-ok "$dhrystone_ok"
  --log-paths-json "$log_paths_json"
)
if [[ -n "$haydn_lit_log" && -f "$haydn_lit_log" ]]; then
  writer_args+=(--haydn-lit-log "$haydn_lit_log")
else
  # No log → fail-closed lit (not a silent green).
  writer_args+=(--haydn-lit-failed 1 --haydn-lit-xpass 0)
fi

set +e
python3 "$WRITER" "${writer_args[@]}" | tee -a "$SUMMARY"
writer_rc=${PIPESTATUS[0]}
set -e

log "======== VERDICT $VERDICT ========"
if [[ -f "$VERDICT" ]]; then
  log "$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print("overall="+d["overall"])' "$VERDICT")"
fi
exit "$writer_rc"
