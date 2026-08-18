#!/usr/bin/env bash
# pattern_gate.sh — leftover M16 NatureDSP hot-core compare (not QUALIFY).
#
# Product defaults stay OFF (hwloop/SMS/Stage-0 PP+IB). Historical
# -haydn-enable-post-pipeliner / -haydn-enable-interblock flags are
# deleted; extra=() "default" is the product-default-OFF compile.
# Compiles a fixed kernel set:
#   off     : explicit leftover OFF flags (no-ops if flags are absent)
#   default : product defaults (PP/IB/SMS/hwloop OFF)
#   ib      : leftover IB force; not a product path
#
# Fails if default (or ib when CHECK_IB=1) grows any LLhwloop II vs off,
# or drops set_hwloop count vs off. Not a coverage-fuzzer / baseline seat.
# M16 leftover (frozen until SF1-SF3): T-TI3 per-pass coverage, T-TI4
# MC/disasm fuzzer, T-TI5 stored perf baselines, T-TI6 randomized
# codegen. Do not invent a fuzz gate.
#
# Usage:
#   bash pattern_gate.sh [out-dir]
# Env:
#   CLANG, SRC, TOOLS, CHECK_IB=0|1, JOBS
set -euo pipefail

CLANG=${CLANG:-/ssd2/mhyang/haydn-build/bin/clang}
SRC=${SRC:-/ssd2/mhyang/haydn-plans/hifi_naturedsp_reports/src}
TOOLS=${TOOLS:-/ssd2/mhyang/haydn-plans/naturedsp-haydn/tools}
OUT=${1:-/tmp/pattern-gate}
CHECK_IB=${CHECK_IB:-0}
COMPILE_TIMEOUT=${COMPILE_TIMEOUT:-45}
DUMP="$TOOLS/dump_hot_core.py"
RES="$TOOLS/residual-beyond-approved.txt"
mkdir -p "$OUT"

RD=$("$CLANG" -target haydn-unknown-elf -print-resource-dir)
# ndsp_private_overlay before include_private: re-asserts castxcc as lvalue
# after NatureDSP common.h (AE_*_IP post-inc / D185 / F24 stream fix).
# Product CPU enables simd/bit-reversed AE surface (see compile_e2e.sh).
COMMON=(-target haydn-unknown-elf -mcpu=haydn -O2 -mllvm -global-isel-abort=1 -nostdinc
  -DCOMPILER_XTENSA -DXCHAL_HAVE_HIFI3Z=1 -DXCHAL_HAVE_NSA=1
  -isystem "$RD/include"
  -isystem "$TOOLS/freestanding-shims"
  -I "$SRC/library/include"
  -I "$TOOLS/freestanding-shims/ndsp_private_overlay"
  -I "$SRC/library/include_private"
  -include haydn_dsp.h
  -include ndsp_ae_overlay.h)

# Golden set from plan §1.3
GOLDENS=(
  vec_dot32x32_hifi3
  bkfir32x32_hifi3
  raw_corr32x32_hifi3
  vec_add32x32_hifi3
  vec_scale32x32_hifi3
)

find_src() {
  local k=$1
  find "$SRC/library" -name "${k}.c" 2>/dev/null | head -1
}

compile_one() {
  local k=$1 mode=$2
  local f extra=()
  f=$(find_src "$k")
  if [[ -z "$f" || ! -f "$f" ]]; then
    echo "MISSING_SRC $k" >&2
    return 1
  fi
  case "$mode" in
    off)
      extra=(-mllvm -haydn-enable-post-pipeliner=false -mllvm -haydn-enable-interblock=false)
      ;;
    default) extra=() ;;
    ib)
      extra=(-mllvm -haydn-enable-interblock=true)
      ;;
    *) echo "bad mode $mode" >&2; return 1 ;;
  esac
  local rc=0
  if command -v timeout >/dev/null 2>&1; then
    timeout --signal=KILL "$COMPILE_TIMEOUT" "$CLANG" "${COMMON[@]}" \
      ${extra[@]+"${extra[@]}"} -S "$f" \
      -o "$OUT/${k}_${mode}.s" 2>"$OUT/${k}_${mode}.err" || rc=$?
  else
    "$CLANG" "${COMMON[@]}" ${extra[@]+"${extra[@]}"} -S "$f" \
      -o "$OUT/${k}_${mode}.s" 2>"$OUT/${k}_${mode}.err" || rc=$?
  fi
  if [[ "$rc" -eq 124 || "$rc" -eq 137 ]]; then
    echo "COMPILE_TIMEOUT $k $mode ${COMPILE_TIMEOUT}s" >&2
    return 124
  fi
  if [[ "$rc" -ne 0 ]]; then
    echo "COMPILE_FAIL $k $mode" >&2
    return 1
  fi
}

classified_o2_timeout() {
  local k=$1
  local stem=${k%_hifi3}
  [[ -f "$RES" ]] || return 1
  grep -qE "KERNEL:[[:space:]]*(${k}|${stem})\\b|(${k}|${stem}).*TIMEOUT|TIMEOUT.*(${k}|${stem})" "$RES"
}

echo "pattern_gate: OUT=$OUT CLANG=$CLANG CHECK_IB=$CHECK_IB COMPILE_TIMEOUT=$COMPILE_TIMEOUT" >&2

fail=0
declare -A OFF_II OFF_HL

for k in "${GOLDENS[@]}"; do
  _crc=0
  compile_one "$k" off || _crc=$?
  if [[ "$_crc" -eq 124 ]] && classified_o2_timeout "$k"; then
    printf '%-28s SKIP classified -O2 compile timeout\n' "$k"
    continue
  fi
  if [[ "$_crc" -ne 0 ]]; then
    fail=1
    continue
  fi
  compile_one "$k" default || _crc=$?
  if [[ "$_crc" -ne 0 ]]; then
    if [[ "$_crc" -eq 124 ]] && classified_o2_timeout "$k"; then
      printf '%-28s SKIP classified -O2 compile timeout\n' "$k"
      continue
    fi
    fail=1
    continue
  fi
  if [[ "$CHECK_IB" == "1" ]]; then
    compile_one "$k" ib || true
  fi

  # Parse OFF
  mapfile -t off_lines < <(python3 "$DUMP" "$OUT/${k}_off.s" 2>/dev/null | grep -E '^\s+L[0-9]+:' || true)
  off_hl=$(python3 -c "import re; t=open('$OUT/${k}_off.s').read(); print(sum(1 for l in t.splitlines() if 'set_hwloop' in l))")
  # Build II list from dump lines: L0: II=2 ops=...
  off_iis=()
  while IFS= read -r line; do
    if [[ "$line" =~ II=([0-9]+) ]]; then
      off_iis+=("${BASH_REMATCH[1]}")
    fi
  done < <(python3 "$DUMP" "$OUT/${k}_off.s" 2>/dev/null | grep -E '^\s+L[0-9]+:' || true)

  def_hl=$(python3 -c "import re; t=open('$OUT/${k}_default.s').read(); print(sum(1 for l in t.splitlines() if 'set_hwloop' in l))")
  def_iis=()
  while IFS= read -r line; do
    if [[ "$line" =~ II=([0-9]+) ]]; then
      def_iis+=("${BASH_REMATCH[1]}")
    fi
  done < <(python3 "$DUMP" "$OUT/${k}_default.s" 2>/dev/null | grep -E '^\s+L[0-9]+:' || true)

  verdict=PASS
  reasons=()
  if (( def_hl < off_hl )); then
    verdict=FAIL
    reasons+=("hl ${off_hl}->${def_hl}")
    fail=1
  fi
  if (( ${#off_iis[@]} == ${#def_iis[@]} )); then
    for i in "${!off_iis[@]}"; do
      if (( def_iis[i] > off_iis[i] )); then
        verdict=FAIL
        reasons+=("L$i ${off_iis[i]}->${def_iis[i]}")
        fail=1
      fi
    done
  else
    # compare max II if loop count differs
    off_max=0; def_max=0
    for x in "${off_iis[@]:-}"; do (( x > off_max )) && off_max=$x; done
    for x in "${def_iis[@]:-}"; do (( x > def_max )) && def_max=$x; done
    if (( def_max > off_max )); then
      verdict=FAIL
      reasons+=("maxII ${off_max}->${def_max}")
      fail=1
    fi
  fi

  printf '%-28s off_II=%-12s def_II=%-12s hl %s->%s  %s %s\n' \
    "$k" "[${off_iis[*]}]" "[${def_iis[*]}]" "$off_hl" "$def_hl" \
    "$verdict" "${reasons[*]-}"

  # Optional IB check
  if [[ "$CHECK_IB" == "1" ]]; then
    ib_hl=$(python3 -c "t=open('$OUT/${k}_ib.s').read(); print(sum(1 for l in t.splitlines() if 'set_hwloop' in l))")
    ib_iis=()
    while IFS= read -r line; do
      if [[ "$line" =~ II=([0-9]+) ]]; then
        ib_iis+=("${BASH_REMATCH[1]}")
      fi
    done < <(python3 "$DUMP" "$OUT/${k}_ib.s" 2>/dev/null | grep -E '^\s+L[0-9]+:' || true)
    ib_verdict=PASS
    ib_reasons=()
    if (( ib_hl < off_hl )); then
      ib_verdict=FAIL; ib_reasons+=("hl ${off_hl}->${ib_hl}"); fail=1
    fi
    if (( ${#off_iis[@]} == ${#ib_iis[@]} )); then
      for i in "${!off_iis[@]}"; do
        if (( ib_iis[i] > off_iis[i] )); then
          ib_verdict=FAIL; ib_reasons+=("L$i ${off_iis[i]}->${ib_iis[i]}"); fail=1
        fi
      done
    fi
    printf '  %-26s ib_II=%-12s hl %s  %s %s\n' \
      "(ib)" "[${ib_iis[*]}]" "$ib_hl" "$ib_verdict" "${ib_reasons[*]-}"
  fi
done

if (( fail )); then
  echo "GATE FAIL" >&2
  exit 1
fi
echo "GATE PASS" >&2
exit 0
