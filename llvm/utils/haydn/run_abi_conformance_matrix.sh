#!/usr/bin/env bash
# run_abi_conformance_matrix.sh — T-ABI9 monorepo seat (M6).
#
# Compiles and links the two-TU ABI matrix plus the atomics symbol probe
# against the matching freestanding sysroot, then binds those objects with
# haydn-rt/haydn.ld into an executable (no BundleSim run). This is
# compile/link evidence on one artifact, not semantic QUALIFY.
# CoreMark/Dhrystone consumer-C TARGET_BUILD_FAILED is a classified residual.
# The consumer ctest label `abi-conformance` remains the executed residual.
#
# Exit 0 when every case compiles, links (when sysroot+ld exist), has
# non-empty .text, and e_flags == 0x1. Exit 1 on pin fail. Exit 2 on tools.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/abi-conformance"
CLANG=${CLANG:-${HAYDN_BIN:+$HAYDN_BIN/clang}}
CLANG=${CLANG:-/ssd2/mhyang/haydn-build/bin/clang}
[[ -x "$CLANG" ]] || { echo "abi-matrix: FAIL no clang ($CLANG)" >&2; exit 2; }
HAYDN_BIN="$(cd "$(dirname "$CLANG")" && pwd)"
export HAYDN_BIN PATH="$HAYDN_BIN:$PATH"

OUT=${1:-/tmp/haydn-abi-conformance}
mkdir -p "$OUT"
REQUIRE_LINK=${ABI_MATRIX_REQUIRE_LINK:-1}
REQUIRE_ARTIFACT=${ABI_MATRIX_REQUIRE_ARTIFACT:-1}

_sysroot=""
for try in \
  "${BUNDLESIM_SYSROOT:-}" \
  "${HAYDN_SYSROOT:-}" \
  "$HAYDN_BIN/../sysroot/haydn-unknown-elf"
do
  [[ -n "$try" && -d "$try" ]] || continue
  _sysroot="$(cd "$try" && pwd)"
  break
done

COMMON=(-target haydn-unknown-elf -mcpu=haydn -O2 -mllvm -global-isel-abort=1
  -ffreestanding -fno-builtin)

check_obj() {
  local obj=$1 label=$2
  local text flags
  text=$("$HAYDN_BIN/llvm-size" -A "$obj" | awk '$1 == ".text" { print $2; exit }')
  if [[ -z "${text:-}" || "$text" -lt 12 ]]; then
    echo "  FAIL: $label .text=${text:-0} below one Format E parcel" >&2
    return 1
  fi
  flags=$("$HAYDN_BIN/llvm-readobj" --file-headers "$obj" 2>/dev/null |
    awk '/Flags \[/ { gsub(/[^0-9a-fxA-FX]/,"",$NF); print $NF; exit }')
  if [[ "$flags" != "0x1" && "$flags" != "0x01" && "$flags" != "1" ]]; then
    if ! "$HAYDN_BIN/llvm-readobj" --file-headers "$obj" 2>/dev/null |
      grep -qE 'Flags \[ \(0x1\)'; then
      echo "  FAIL: $label e_flags != 0x1 (got ${flags:-unknown})" >&2
      return 1
    fi
  fi
  echo "  PASS: $label text=$text e_flags=0x1"
  return 0
}

fail=0
pass_n=0
echo "======== ABI conformance matrix (compile/link, T-ABI9 monorepo) ========"
echo "CLANG=$CLANG OUT=$OUT SYSROOT=${_sysroot:-none}"
echo "INFO: compile/link + e_flags identity; not semantic QUALIFY"

if ! "$CLANG" "${COMMON[@]}" -c "$SRC_DIR/callee.c" -o "$OUT/callee.o" \
     2>"$OUT/callee.err"; then
  echo "  FAIL: callee compile" >&2
  head -5 "$OUT/callee.err" >&2 || true
  fail=1
else
  if check_obj "$OUT/callee.o" callee; then pass_n=$((pass_n + 1)); else fail=1; fi
fi

if ! "$CLANG" "${COMMON[@]}" -c "$SRC_DIR/caller.c" -o "$OUT/caller.o" \
     2>"$OUT/caller.err"; then
  echo "  FAIL: caller compile" >&2
  head -5 "$OUT/caller.err" >&2 || true
  fail=1
else
  if check_obj "$OUT/caller.o" caller; then pass_n=$((pass_n + 1)); else fail=1; fi
fi

if ! "$CLANG" "${COMMON[@]}" -c "$SRC_DIR/atomics_symbols.c" -o "$OUT/atomics.o" \
     2>"$OUT/atomics.err"; then
  echo "  FAIL: atomics compile" >&2
  head -5 "$OUT/atomics.err" >&2 || true
  fail=1
else
  if check_obj "$OUT/atomics.o" atomics; then pass_n=$((pass_n + 1)); else fail=1; fi
  if ! "$HAYDN_BIN/llvm-nm" "$OUT/atomics.o" 2>/dev/null |
    grep -qE '__atomic_(load|store|exchange)'; then
    echo "  FAIL: atomics object missing __atomic_* symbols" >&2
    fail=1
  else
    echo "  PASS: atomics __atomic_* symbols present"
    pass_n=$((pass_n + 1))
  fi
fi

if ! "$CLANG" "${COMMON[@]}" -c "$SRC_DIR/start.c" -o "$OUT/start.o" \
     2>"$OUT/start.err"; then
  echo "  FAIL: start compile" >&2
  head -5 "$OUT/start.err" >&2 || true
  fail=1
else
  if check_obj "$OUT/start.o" start; then pass_n=$((pass_n + 1)); else fail=1; fi
  if ! "$HAYDN_BIN/llvm-nm" "$OUT/start.o" 2>/dev/null | grep -q ' T _start'; then
    echo "  FAIL: start object missing _start" >&2
    fail=1
  else
    echo "  PASS: start _start symbol present"
    pass_n=$((pass_n + 1))
  fi
fi

LD=""
if [[ -x "$HAYDN_BIN/ld.lld" ]]; then LD="$HAYDN_BIN/ld.lld"
elif [[ -x "$HAYDN_BIN/lld" ]]; then LD="$HAYDN_BIN/lld"
fi

if [[ -n "$LD" && -f "$OUT/callee.o" && -f "$OUT/caller.o" ]]; then
  # Relocatable two-TU link proves caller/callee symbol resolution on the
  # product object ABI without pulling BSP crt0 / libc abort. Full
  # executable execution stays on the BundleSim ctest residual.
  _link_out="$OUT/two_tu.o"
  if "$LD" -r -o "$_link_out" "$OUT/callee.o" "$OUT/caller.o" \
       >"$OUT/two_tu.link.log" 2>&1; then
    if check_obj "$_link_out" two-tu-reloc; then
      if "$HAYDN_BIN/llvm-nm" "$_link_out" 2>/dev/null | grep -q ' T ret_i32' &&
         "$HAYDN_BIN/llvm-nm" "$_link_out" 2>/dev/null | grep -q ' T consume' &&
         "$HAYDN_BIN/llvm-nm" "$_link_out" 2>/dev/null | grep -q ' T ret_f32' &&
         "$HAYDN_BIN/llvm-nm" "$_link_out" 2>/dev/null | grep -q ' T ret_f64'; then
        echo "  PASS: two-TU reloc resolves ret_i32/ret_f32/ret_f64 + consume"
        pass_n=$((pass_n + 1))
      else
        echo "  FAIL: two-TU reloc missing ret_i32/ret_f32/ret_f64/consume" >&2
        fail=1
      fi
    else
      fail=1
    fi
  else
    echo "  FAIL: two-TU relocatable link" >&2
    tail -8 "$OUT/two_tu.link.log" >&2 || true
    fail=1
  fi
else
  if [[ "$REQUIRE_LINK" == "1" ]]; then
    echo "  FAIL: two-TU link required but ld/objects missing" >&2
    fail=1
  else
    echo "  INFO: two-TU link skipped (ld/objects absent)"
  fi
fi

# Product-ld executable bind: in-tree haydn-rt/haydn.ld (or matching
# sysroot copy). This is compiler→LLD→product-script identity, not
# BundleSim execution and not semantic QUALIFY. BSP crt0 stays unused.
_llvm_src="$(cd "$SCRIPT_DIR/../../.." && pwd)"
_product_ld=""
for try in \
  "$_llvm_src/haydn-rt/haydn.ld" \
  "${_sysroot:+$_sysroot/lib/haydn.ld}" \
  "${_sysroot:+$_sysroot/lib/bundlesim.ld}"
do
  [[ -n "$try" && -f "$try" ]] || continue
  _product_ld="$try"
  break
done
if [[ -n "$LD" && -n "$_product_ld" && -f "$OUT/start.o" &&
      -f "$OUT/callee.o" && -f "$OUT/caller.o" ]]; then
  _elf="$OUT/two_tu.elf"
  _link_cmd=("$LD" --nmagic -T "$_product_ld" -o "$_elf"
    "$OUT/start.o" "$OUT/callee.o" "$OUT/caller.o")
  if [[ -n "$_sysroot" && -f "$_sysroot/lib/libclang_rt.builtins-haydn.a" ]]; then
    _link_cmd+=("$_sysroot/lib/libclang_rt.builtins-haydn.a")
  elif [[ -n "$_sysroot" && -f "$_sysroot/lib/libclang_rt.builtins.a" ]]; then
    _link_cmd+=("$_sysroot/lib/libclang_rt.builtins.a")
  fi
  if "${_link_cmd[@]}" >"$OUT/two_tu.elf.link.log" 2>&1; then
    if check_obj "$_elf" product-ld-elf; then
      _hdr=$("$HAYDN_BIN/llvm-readobj" --file-headers "$_elf")
      _entry=$(printf '%s\n' "$_hdr" | awk '/Entry:/ { print $2; exit }')
      _mach=$(printf '%s\n' "$_hdr" | awk '/Machine:/ { print $2; exit }')
      if [[ "$_entry" == "0x10000" && "$_mach" == "0x103" ]]; then
        echo "  PASS: product-ld executable EM=0x103 entry=0x10000"
        pass_n=$((pass_n + 1))
      else
        echo "  FAIL: product-ld executable entry=${_entry:-?} machine=${_mach:-?}" >&2
        fail=1
      fi
    else
      fail=1
    fi
  else
    echo "  FAIL: product-ld executable link ($_product_ld)" >&2
    tail -8 "$OUT/two_tu.elf.link.log" >&2 || true
    fail=1
  fi
else
  if [[ "$REQUIRE_LINK" == "1" ]]; then
    echo "  FAIL: product-ld executable link required but ld/script/objects missing" >&2
    fail=1
  else
    echo "  INFO: product-ld executable link skipped"
  fi
fi

if [[ -n "$_sysroot" && -f "$_sysroot/ARTIFACT.json" ]]; then
  _aid="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("artifact_id",""))' \
    "$_sysroot/ARTIFACT.json" 2>/dev/null || true)"
  _pld="$(python3 -c 'import json,sys; b=json.load(open(sys.argv[1])).get("product_ld") or {}; print(b.get("authority","") if isinstance(b,dict) else "")' \
    "$_sysroot/ARTIFACT.json" 2>/dev/null || true)"
  if [[ -n "$_aid" && ${#_aid} -eq 64 ]]; then
    echo "  PASS: artifact_id=${_aid}"
    pass_n=$((pass_n + 1))
  else
    echo "  FAIL: ARTIFACT.json present but artifact_id incomplete" >&2
    if [[ "$REQUIRE_ARTIFACT" == "1" ]]; then
      fail=1
    fi
  fi
  if [[ "$_pld" == "haydn-rt/haydn.ld" ]]; then
    echo "  PASS: ARTIFACT.product_ld=haydn-rt/haydn.ld"
    pass_n=$((pass_n + 1))
  else
    echo "  FAIL: ARTIFACT.product_ld missing/null (bind with --install-product-ld)" >&2
    if [[ "$REQUIRE_ARTIFACT" == "1" ]]; then
      fail=1
    fi
  fi
else
  if [[ "$REQUIRE_ARTIFACT" == "1" ]]; then
    echo "  FAIL: no ARTIFACT.json next to toolchain" >&2
    fail=1
  else
    echo "  INFO: ARTIFACT.json optional for this pin"
  fi
fi

echo "======== ABI matrix done pass=$pass_n fail=$fail ========"
[[ "$fail" -eq 0 ]]
