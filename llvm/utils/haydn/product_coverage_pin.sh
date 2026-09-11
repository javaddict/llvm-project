#!/usr/bin/env bash
# product_coverage_pin.sh — monorepo seat for T-AUTHORITY-EVIDENCE /
# G-SOURCE-ALIGNMENT (+ G-TEST-EVIDENCE / G-RUNTIME-TOOLCHAIN residual).
#
# Aggregates product evidence surfaces under G-TEST-EVIDENCE /
# G-RUNTIME-TOOLCHAIN / G-LIBRARY-COVERAGE / G-DEBUG-OBSERVABILITY /
# G-ECOSYSTEM-CONSUMERS without inventing qualification labels
# (T8-DEBUG-EVIDENCE: M10 step-inst never qualified; DG0 inventory only):
#   * XFAIL ledger parity (Inputs/XFAIL-OWNER-LEDGER.txt)
#   * freestanding sysroot libc/libm + ARTIFACT.json 64-hex stamp
#   * five-file catalog provenance (GOLDEN_INPUTS + generate_catalog --check
#     + ARTIFACT.catalog complete; retired 7-slot / 17-file pins fail-closed)
#   * NatureDSP product_library_pin (default expand; MIN_TEXT + EF_HAYDN_E96;
#     mandatory when NatureDSP sources are present)
#   * focused lit seats (language matrix, hostile-byte, nested unwind, eflags,
#     plus owned runtime/debug: c-e2e, debug-info, cfi, baremetal-startup)
#   * parse_lit_summary self-test (Failed vs Expectedly Failed hygiene)
#   * ARTIFACT.debug.step_inst + decode/consumer identity + decode live bind
#   * T-SF4 FP-in-gate compiler-rt helpers + yarpgen 28-seed
#   * gcc-torture FP skip classified (not silent) + T-ABI9 compile/link matrix
#   * write_ci_verdict schema (PASS/FAIL only; never semantic QUALIFY)
#   * NatureDSP executed-canary compile pin (vec_dot16 T-DSP3 residual)
#   * NatureDSP 456-beyond / 35-approved / 491-total hifi3 census
#   * T-DSP12 default-CPU + T-DSP13 declared-vs-tested residual inventory
#   * tdsp13_declared_vs_tested_pin.py (inventory only; no 746-name harness)
#   * ARTIFACT.library libc+libm identity (product_library_pin residual)
#   * ARTIFACT.product_ld + ld.lld live bind (same-artifact linker seat)
#   * T-SF9 op×type×symbol contract inventory (no TestFloat invent)
#   * T-SF10 hygiene + frozen-28 yarpgen (not a fuzz gate)
#   * M2 KPI re-sweep measured-miss (CompleteModel=0; no competitive II)
#   * HDR-SPLIT measured miss (one file; no Stage-0 split/densify)
#   * library-coverage residual selfcheck (no second matrix)
#   * haydn-rt M1 scale32/shift32 + T-SF9 contract + product identity
#
# DG0 DecisionGuard product registry stays absent by design — ownership is
# Inputs/* inventories + this pin + check_xfail_ledger / parse_lit_summary.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LLVM_SRC="$(cd "$SCRIPT_DIR/../../.." && pwd)"
HAYDN_BIN="${HAYDN_BIN:-${BUNDLESIM_HAYDN_TOOLCHAIN_BIN:-/ssd2/mhyang/haydn-build/bin}}"
export HAYDN_BIN PATH="$HAYDN_BIN:$PATH"
fail=0
echo "======== haydn product coverage pin (T-AUTHORITY-EVIDENCE) ========"
echo "LLVM_SRC=$LLVM_SRC HAYDN_BIN=$HAYDN_BIN"

if [[ -x "$SCRIPT_DIR/check_xfail_ledger.py" ]]; then
  if python3 "$SCRIPT_DIR/check_xfail_ledger.py" --llvm-src "$LLVM_SRC"; then
    echo "  PASS: xfail ledger parity"
  else echo "  FAIL: xfail ledger parity" >&2; fail=1; fi
  if python3 "$SCRIPT_DIR/check_xfail_ledger.py" --inventory-pin --llvm-src "$LLVM_SRC"; then
    echo "  PASS: PIPE-20/DG0/M16 inventory-only pin"
  else echo "  FAIL: PIPE-20/DG0/M16 inventory-only pin" >&2; fail=1; fi
  if python3 "$SCRIPT_DIR/check_xfail_ledger.py" --runtime-pin --llvm-src "$LLVM_SRC"; then
    echo "  PASS: haydn-rt M1/M10/M13/M15 contract pin"
  else echo "  FAIL: haydn-rt M1/M10/M13/M15 contract pin" >&2; fail=1; fi
else echo "  FAIL: check_xfail_ledger.py missing" >&2; fail=1; fi

if [[ -x "$SCRIPT_DIR/parse_lit_summary.py" ]]; then
  if python3 "$SCRIPT_DIR/parse_lit_summary.py" --self-test >/tmp/haydn-parse-lit-self.log 2>&1; then
    echo "  PASS: parse_lit_summary self-test"
  else
    echo "  FAIL: parse_lit_summary self-test" >&2
    tail -20 /tmp/haydn-parse-lit-self.log >&2 || true
    fail=1
  fi
  # Historical 08-17 full-gate manifest is not an ancestor.
  if printf '%s\n' 'Passed           :   1' 'git_commit: 28700d57366a35a7d04e8adfbdf782743ec847e0' \
       | python3 "$SCRIPT_DIR/parse_lit_summary.py" --refuse-stale-commit \
            --require-ancestor --llvm-src "$LLVM_SRC" - \
            >/tmp/haydn-parse-lit-stale.log 2>&1; then
    echo "  FAIL: parse_lit_summary must refuse 28700d57" >&2
    fail=1
  else
    echo "  PASS: parse_lit_summary refuses 28700d57 (not an ancestor)"
  fi
  if printf '%s\n' 'Passed           :   1' 'git_commit: 4b6677f8bf87d12f20e4d0b0ff5ec10124dddf2f' \
       | python3 "$SCRIPT_DIR/parse_lit_summary.py" --require-ancestor \
            --llvm-src "$LLVM_SRC" - \
            >/tmp/haydn-parse-lit-rebind.log 2>&1; then
    echo "  PASS: parse_lit_summary accepts repo-resident 4b6677f8bf87"
  else
    echo "  FAIL: parse_lit_summary must accept in-repo 4b6677f8bf87" >&2
    tail -10 /tmp/haydn-parse-lit-rebind.log >&2 || true
    fail=1
  fi
else echo "  FAIL: parse_lit_summary.py missing" >&2; fail=1; fi

# T-MC10 / M12: in-tree product linker script ident + sysroot body match.
if [[ -x "$SCRIPT_DIR/check_product_ld.py" || -f "$SCRIPT_DIR/check_product_ld.py" ]]; then
  if python3 "$SCRIPT_DIR/check_product_ld.py" --llvm-src "$LLVM_SRC" --require-sysroot; then
    echo "  PASS: product linker script pin (T-MC10)"
  else echo "  FAIL: product linker script pin (T-MC10)" >&2; fail=1; fi
else echo "  FAIL: check_product_ld.py missing" >&2; fail=1; fi

# Same-artifact recorder: product-ld bind + install script (not BSP-only).
if [[ -f "$SCRIPT_DIR/record_haydn_artifact_set.py" ]]; then
  if python3 "$SCRIPT_DIR/record_haydn_artifact_set.py" --self-test \
       >/tmp/haydn-product-coverage-recorder-self.log 2>&1; then
    echo "  PASS: record_haydn_artifact_set self-test"
  else
    echo "  FAIL: record_haydn_artifact_set self-test" >&2
    tail -20 /tmp/haydn-product-coverage-recorder-self.log >&2 || true
    fail=1
  fi
  if python3 "$SCRIPT_DIR/record_haydn_artifact_set.py" --check-product-ld \
       --llvm-src "$LLVM_SRC" --require-sysroot --require-consumer-install \
       >/tmp/haydn-product-coverage-recorder-ld.log 2>&1; then
    echo "  PASS: product-ld same-artifact bind (haydn-rt/haydn.ld)"
    if grep -q 'T7-RT residual' /tmp/haydn-product-coverage-recorder-ld.log; then
      echo "  FAIL: T7-RT committed consumer install unbound" >&2
      fail=1
    elif grep -q 'T7-RT committed install binds' /tmp/haydn-product-coverage-recorder-ld.log; then
      echo "  PASS: T7-RT committed product-ld bind (edbb813)"
    fi
  else
    echo "  FAIL: product-ld same-artifact bind" >&2
    tail -20 /tmp/haydn-product-coverage-recorder-ld.log >&2 || true
    fail=1
  fi
  if python3 "$SCRIPT_DIR/record_haydn_artifact_set.py" --check-library \
       --llvm-src "$LLVM_SRC" --require-sysroot \
       >/tmp/haydn-product-coverage-recorder-lib.log 2>&1; then
    echo "  PASS: library same-artifact identity (libc+libm / product_library_pin)"
  else
    echo "  FAIL: library same-artifact identity" >&2
    tail -20 /tmp/haydn-product-coverage-recorder-lib.log >&2 || true
    fail=1
  fi
else
  echo "  FAIL: record_haydn_artifact_set.py missing" >&2
  fail=1
fi

# F21 hygiene: inventory residual/transitional/legacy prose and RESIDUAL(goal-N).
# Print only — do not fail the pin on a high count this wave.
_haydn_src="$LLVM_SRC/llvm/lib/Target/Haydn"
if [[ -d "$_haydn_src" ]]; then
  _prose_n=$( { grep -RInE --include='*.cpp' --include='*.h' --include='*.inc' --include='*.td' \
    -e 'residual|transitional|legacy' "$_haydn_src" || true; } | wc -l | tr -d ' ')
  _residual_n=$( { grep -RInE --include='*.cpp' --include='*.h' --include='*.inc' --include='*.td' \
    -e '// RESIDUAL\(' "$_haydn_src" || true; } | wc -l | tr -d ' ')
  echo "  INFO: F21 debt inventory residual|transitional|legacy=${_prose_n} RESIDUAL(goal-N)=${_residual_n}"
else
  echo "  INFO: F21 debt inventory skipped (Haydn sources absent)"
fi

_sysroot=""
for try in "${BUNDLESIM_SYSROOT:-}" "${HAYDN_SYSROOT:-}" "$HAYDN_BIN/../sysroot/haydn-unknown-elf"; do
  [[ -n "$try" && -d "$try" ]] || continue
  _sysroot="$(cd "$try" && pwd)"; break
done
if [[ -z "$_sysroot" ]]; then echo "  FAIL: freestanding sysroot not found" >&2; fail=1
else
  [[ -f "$_sysroot/lib/libc.a" ]] && echo "  PASS: libc.a present" || { echo "  FAIL: missing libc.a" >&2; fail=1; }
  [[ -f "$_sysroot/lib/libm.a" ]] && echo "  PASS: libm.a present" || { echo "  FAIL: missing libm.a" >&2; fail=1; }
  _checker="${BUNDLESIM_ROOT:-/ssd2/mhyang/BundleSim}/scripts/check_haydn_artifact_set.sh"
  if [[ -f "$_checker" ]]; then
    if BUNDLESIM_REQUIRE_ARTIFACT=1 BUNDLESIM_ARTIFACT_AUTO_RESTAMP=1 \
       bash "$_checker" "$_sysroot" >/tmp/haydn-product-coverage-artifact-check.log 2>&1; then
      echo "  PASS: ARTIFACT pin (identity restamp allowed)"
    else
      echo "  FAIL: ARTIFACT pin / restamp" >&2
      tail -20 /tmp/haydn-product-coverage-artifact-check.log >&2 || true
      fail=1
    fi
  fi
  if [[ -f "$_sysroot/ARTIFACT.json" ]]; then
    _aid="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("artifact_id",""))' "$_sysroot/ARTIFACT.json" 2>/dev/null || true)"
    _llvm="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("llvm_src",{}).get("git_commit",""))' "$_sysroot/ARTIFACT.json" 2>/dev/null || true)"
    if [[ -n "$_aid" && ${#_aid} -eq 64 ]]; then
      echo "  PASS: artifact_id=${_aid}"
      if [[ -n "$_llvm" ]]; then echo "  INFO: ARTIFACT llvm_src.git_commit=${_llvm}"; fi
      # Rebound: 08-17 full-gate manifest git_commit=28700d57 is not an
      # ancestor. ARTIFACT must name an in-repo commit (4b6677f8bf87 at this stamp).
      if [[ "${_llvm}" == 28700d57* ]]; then
        echo "  FAIL: ARTIFACT rebound required (28700d57 is not an ancestor)" >&2
        fail=1
      elif [[ -n "$_llvm" ]]; then
        if git -C "$LLVM_SRC" merge-base --is-ancestor "$_llvm" HEAD 2>/dev/null; then
          echo "  PASS: ARTIFACT git_commit=${_llvm} is in-repo ancestor"
        else
          echo "  FAIL: ARTIFACT git_commit=${_llvm} is not an ancestor of HEAD" >&2
          fail=1
        fi
      else
        echo "  FAIL: ARTIFACT llvm_src.git_commit missing (cannot rebound)" >&2
        fail=1
      fi
    else echo "  FAIL: ARTIFACT.json incomplete" >&2; fail=1; fi
    # Consumer restamp can drop monorepo-owned library/product_ld. Re-bind
    # haydn-rt/haydn.ld, refuse .bak/.broken debris, and re-attach stamps.
    if [[ -f "$SCRIPT_DIR/record_haydn_artifact_set.py" ]]; then
      if python3 "$SCRIPT_DIR/record_haydn_artifact_set.py" --install-product-ld \
           --llvm-src "$LLVM_SRC" --sysroot "$_sysroot" \
           >/tmp/haydn-product-coverage-recorder-attach.log 2>&1; then
        echo "  PASS: product-ld install + ARTIFACT attach-owned"
      else
        echo "  FAIL: product-ld install / attach-owned" >&2
        tail -20 /tmp/haydn-product-coverage-recorder-attach.log >&2 || true
        fail=1
      fi
    fi
    # Catalog block must be five-file complete and must not claim retired inputs.
    if ! python3 - "$_sysroot/ARTIFACT.json" >/tmp/haydn-product-coverage-artifact-cat.log 2>&1 <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
cat = doc.get("catalog") or {}
pin = cat.get("golden_inputs_pin") or {}
errs = []
if not cat:
    errs.append("catalog.missing")
else:
    if cat.get("authority") != "five-file":
        errs.append("authority!=five-file")
    if cat.get("retired_inputs_consumed"):
        errs.append("retired_inputs_consumed")
    if not cat.get("complete"):
        errs.append("catalog.incomplete")
    if not pin.get("five_file_complete") or int(pin.get("file_count") or 0) != 5:
        errs.append("GOLDEN_INPUTS not five-file")
if errs:
    print("FAIL:" + ",".join(errs))
    sys.exit(1)
sys.exit(0)
PY
    then
      echo "  FAIL: ARTIFACT.catalog not five-file complete / retired inputs" >&2
      tail -10 /tmp/haydn-product-coverage-artifact-cat.log >&2 || true
      fail=1
    else
      echo "  PASS: ARTIFACT.catalog five-file complete"
    fi
    if ! python3 - "$_sysroot/ARTIFACT.json" >/tmp/haydn-product-coverage-artifact-decode.log 2>&1 <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
dec = doc.get("decode") or {}
cons = doc.get("consumer") or {}
errs = []
if dec:
    if not dec.get("complete"):
        errs.append("decode.incomplete")
    if not dec.get("shim_discipline_ok"):
        errs.append("decode.shim_discipline")
    if int(dec.get("shim_count") or 0) != 7:
        errs.append("decode.shim_count")
else:
    errs.append("decode.missing")
dbg = (cons.get("debugger") or {})
if cons and not (dbg.get("sha256") or (doc.get("toolchain") or {}).get("lldb", {}).get("sha256")):
    if cons.get("complete"):
        errs.append("debugger.missing")
if errs:
    print("FAIL:" + ",".join(errs))
    sys.exit(1)
sys.exit(0)
PY
    then
      echo "  FAIL: ARTIFACT.decode/consumer incomplete" >&2
      tail -10 /tmp/haydn-product-coverage-artifact-decode.log >&2 || true
      fail=1
    else
      echo "  PASS: ARTIFACT.decode/Functional_Model + consumer identity"
    fi
  else echo "  FAIL: missing ARTIFACT.json" >&2; fail=1; fi
fi

# --- Five-file consumer catalog provenance (parity with full-gate seats) ---
# Fail-closed: a stale 17-file pin or retired per-slot catalog cannot green.
_bundlesim="${BUNDLESIM_ROOT:-/ssd2/mhyang/BundleSim}"
_golden_dir="${BUNDLESIM_GOLDEN_DIR:-${HAYDN_GOLDEN_DIR:-}}"
if [[ -z "${_golden_dir}" ]]; then
  for _gtry in \
    /ssd/mhyang/dsp/VLIW_Engine_Tool_20260604/AI/Database/golden \
    /ssd2/mhyang/haydn-plans/Database/golden
  do
    if [[ -f "$_gtry/format_e_bit_layout_v2.json" ]]; then
      _golden_dir="$_gtry"
      break
    fi
  done
fi
_golden_pin_sh="$_bundlesim/scripts/check_golden_pin.sh"
_gen_catalog="$_bundlesim/bundlesim/isa/database/generate_catalog.py"
_gen_inc="$_bundlesim/bundlesim/isa/database/generated/instruction_catalog_generated.inc"
if [[ -n "${_golden_dir:-}" && -d "$_golden_dir" && -f "$_golden_pin_sh" ]]; then
  if BUNDLESIM_REQUIRE_GOLDEN=1 bash "$_golden_pin_sh" "$_golden_dir" >/tmp/haydn-product-coverage-golden-pin.log 2>&1; then
    echo "  PASS: GOLDEN five-file pin ($_golden_dir)"
  else
    echo "  FAIL: GOLDEN five-file pin" >&2
    tail -20 /tmp/haydn-product-coverage-golden-pin.log >&2 || true
    fail=1
  fi
  if [[ -f "$_gen_catalog" && -f "$_gen_inc" ]] && command -v python3 >/dev/null 2>&1; then
    if python3 "$_gen_catalog" --golden "$_golden_dir" --output "$_gen_inc" --check \
         >/tmp/haydn-product-coverage-catalog.log 2>&1; then
      echo "  PASS: catalog generate --check (five-file layout fresh)"
    else
      echo "  FAIL: catalog generate --check stale/retired" >&2
      tail -20 /tmp/haydn-product-coverage-catalog.log >&2 || true
      fail=1
    fi
  else
    echo "  FAIL: generate_catalog.py or generated.inc missing under BundleSim" >&2
    fail=1
  fi
elif [[ "${REQUIRE_CATALOG_FIVE_FILE:-1}" == "1" ]]; then
  echo "  FAIL: five-file golden dir / check_golden_pin.sh not discoverable" >&2
  fail=1
else
  echo "  INFO: five-file catalog pin skipped (golden dir unset)"
fi

_isa_pin="$_bundlesim/scripts/check_isa_pin_discipline.sh"
if [[ -f "$_isa_pin" ]]; then
  if bash "$_isa_pin" >/tmp/haydn-product-coverage-isa-pin.log 2>&1; then
    echo "  PASS: Functional_Model / dispatch pin discipline"
  else
    echo "  FAIL: Functional_Model / dispatch pin discipline" >&2
    tail -20 /tmp/haydn-product-coverage-isa-pin.log >&2 || true
    fail=1
  fi
else
  echo "  FAIL: check_isa_pin_discipline.sh missing" >&2
  fail=1
fi

# Same-artifact residual seats (step/hygiene/yarpgen/FP-skip/NatureDSP/freeze).
if [[ -f "$SCRIPT_DIR/check_runtime_artifact_seats.py" ]]; then
  if python3 "$SCRIPT_DIR/check_runtime_artifact_seats.py" --llvm-src "$LLVM_SRC" \
       --require-sysroot --require-consumer-install \
       >/tmp/haydn-product-coverage-runtime-seats.log 2>&1; then
    echo "  PASS: runtime artifact residual seats"
  else
    echo "  FAIL: runtime artifact residual seats" >&2
    tail -20 /tmp/haydn-product-coverage-runtime-seats.log >&2 || true
    fail=1
  fi
else
  echo "  FAIL: check_runtime_artifact_seats.py missing" >&2
  fail=1
fi
if [[ -f "$SCRIPT_DIR/tdsp13_declared_vs_tested_pin.py" ]]; then
  if python3 "$SCRIPT_DIR/tdsp13_declared_vs_tested_pin.py" --llvm-src "$LLVM_SRC" \
       >/tmp/haydn-product-coverage-tdsp13.log 2>&1; then
    echo "  PASS: T-DSP13 declared-vs-tested inventory pin"
  else
    echo "  FAIL: T-DSP13 declared-vs-tested inventory pin" >&2
    tail -20 /tmp/haydn-product-coverage-tdsp13.log >&2 || true
    fail=1
  fi
else
  echo "  FAIL: tdsp13_declared_vs_tested_pin.py missing" >&2
  fail=1
fi
if [[ -f "$SCRIPT_DIR/classify_lldb_step.py" ]]; then
  if python3 "$SCRIPT_DIR/classify_lldb_step.py" --self-test \
       >/tmp/haydn-product-coverage-step-class.log 2>&1; then
    echo "  PASS: LLDB step-inst classifier self-test"
  else
    echo "  FAIL: LLDB step-inst classifier self-test" >&2
    fail=1
  fi
else
  echo "  FAIL: classify_lldb_step.py missing" >&2
  fail=1
fi
if [[ -f "$SCRIPT_DIR/measure_sched_artifact.py" ]]; then
  if python3 "$SCRIPT_DIR/measure_sched_artifact.py" self-test \
       >/tmp/haydn-product-coverage-m2-kpi.log 2>&1; then
    echo "  PASS: M2 KPI measure_sched self-test (measured-miss; not QUALIFY)"
  else
    echo "  FAIL: M2 KPI measure_sched self-test" >&2
    tail -20 /tmp/haydn-product-coverage-m2-kpi.log >&2 || true
    fail=1
  fi
else
  echo "  FAIL: measure_sched_artifact.py missing" >&2
  fail=1
fi

if [[ -f "$SCRIPT_DIR/write_ci_verdict.py" ]]; then
  if python3 "$SCRIPT_DIR/write_ci_verdict.py" --self-test \
       >/tmp/haydn-product-coverage-ci-verdict.log 2>&1; then
    echo "  PASS: CI verdict schema self-test (not semantic QUALIFY)"
  else
    echo "  FAIL: CI verdict schema self-test" >&2
    tail -20 /tmp/haydn-product-coverage-ci-verdict.log >&2 || true
    fail=1
  fi
else
  echo "  FAIL: write_ci_verdict.py missing" >&2
  fail=1
fi

# T-ABI9 monorepo compile/link matrix. Executed ctest label remains residual.
if [[ -x "$SCRIPT_DIR/run_abi_conformance_matrix.sh" || -f "$SCRIPT_DIR/run_abi_conformance_matrix.sh" ]]; then
  if bash "$SCRIPT_DIR/run_abi_conformance_matrix.sh" \
       "${ABI_MATRIX_OUT:-/tmp/haydn-product-coverage-abi}" \
       >/tmp/haydn-product-coverage-abi.log 2>&1; then
    echo "  PASS: ABI conformance compile/link matrix (T-ABI9 monorepo)"
  else
    echo "  FAIL: ABI conformance compile/link matrix" >&2
    tail -20 /tmp/haydn-product-coverage-abi.log >&2 || true
    fail=1
  fi
else
  echo "  FAIL: run_abi_conformance_matrix.sh missing" >&2
  fail=1
fi

_pin="${PRODUCT_LIBRARY_PIN:-/ssd2/mhyang/haydn-plans/naturedsp-haydn/tools/product_library_pin.sh}"
_src="${NATUREDSP_SRC:-/ssd2/mhyang/haydn-plans/hifi_naturedsp_reports/src}"
# When NatureDSP sources are present, library evidence is mandatory (matches
# full-gate G-LIBRARY-COVERAGE). Soft-skip only when the tree is absent.
if [[ -f "$_pin" && -d "$_src/library" ]]; then
  if PRODUCT_LIBRARY_REQUIRE_ARTIFACT=1 PRODUCT_LIBRARY_EXPAND="${PRODUCT_LIBRARY_EXPAND:-1}" \
     SRC="$_src" bash "$_pin" "${PRODUCT_LIBRARY_OUT:-/tmp/haydn-product-coverage-libpin}"; then
    echo "  PASS: NatureDSP product library pin (expand=${PRODUCT_LIBRARY_EXPAND:-1})"
  else echo "  FAIL: NatureDSP product library pin" >&2; fail=1; fi
  _exec_pin="${EXECUTED_CANARY_PIN:-/ssd2/mhyang/haydn-plans/naturedsp-haydn/tools/executed_canary_pin.sh}"
  if [[ -f "$_exec_pin" ]]; then
    if SRC="$_src" bash "$_exec_pin" "${EXECUTED_CANARY_OUT:-/tmp/haydn-product-coverage-exec-canary}"; then
      echo "  PASS: NatureDSP executed-canary compile pin (M1/M3)"
    else echo "  FAIL: NatureDSP executed-canary compile pin" >&2; fail=1; fi
  else
    echo "  FAIL: executed_canary_pin.sh missing" >&2; fail=1
  fi
  _lib_self="${LIBRARY_COVERAGE_SELFCHECK:-/ssd2/mhyang/haydn-plans/naturedsp-haydn/tools/library_coverage_selfcheck.sh}"
  if [[ -f "$_lib_self" ]]; then
    if bash "$_lib_self" >/tmp/haydn-product-coverage-lib-self.log 2>&1; then
      echo "  PASS: NatureDSP library-coverage residual selfcheck"
    else
      echo "  FAIL: NatureDSP library-coverage residual selfcheck" >&2
      tail -20 /tmp/haydn-product-coverage-lib-self.log >&2 || true
      fail=1
    fi
  else
    echo "  FAIL: library_coverage_selfcheck.sh missing" >&2; fail=1
  fi
  _hdr_pin="${HDR_SPLIT_MEASURE:-/ssd2/mhyang/haydn-plans/naturedsp-haydn/tools/hdr_split_measure.sh}"
  if [[ -f "$_hdr_pin" ]]; then
    if bash "$_hdr_pin" >/tmp/haydn-product-coverage-hdr-split.log 2>&1; then
      echo "  PASS: HDR-SPLIT measured miss (one file; no Stage-0)"
    else
      echo "  FAIL: HDR-SPLIT measure" >&2
      tail -10 /tmp/haydn-product-coverage-hdr-split.log >&2 || true
      fail=1
    fi
  else
    echo "  FAIL: hdr_split_measure.sh missing" >&2; fail=1
  fi
elif [[ "${REQUIRE_PRODUCT_LIBRARY:-0}" == "1" || -d "$_src/library" ]]; then
  echo "  FAIL: NatureDSP product library required but pin/sources incomplete" >&2
  fail=1
else echo "  INFO: NatureDSP product library skipped (sources absent)"; fi

LIT=""
if [[ -x "$HAYDN_BIN/llvm-lit" ]]; then LIT="$HAYDN_BIN/llvm-lit"
elif [[ -x /ssd2/mhyang/haydn-build/bin/llvm-lit ]]; then LIT=/ssd2/mhyang/haydn-build/bin/llvm-lit
fi
if [[ -n "$LIT" ]]; then
  # Focused product-evidence lit seats (not full-suite qualification labels).
  _seats=(
    "llvm/test/CodeGen/Haydn/language-coverage-matrix.ll"
    "llvm/test/CodeGen/Haydn/unwind-frame-chain.ll"
    "llvm/test/CodeGen/Haydn/eflags-e96-product-profile.ll"
    "llvm/test/CodeGen/Haydn/xfail-owner-ledger-selfcheck.ll"
    "llvm/test/CodeGen/Haydn/parse-lit-summary-selfcheck.ll"
    "llvm/test/CodeGen/Haydn/Inputs/t6-artifact-selfcheck.ll"
    "llvm/test/CodeGen/Haydn/llc-pipeline-haydn.ll"
    "llvm/test/CodeGen/Haydn/phase-firewall-inventory-pins.ll"
    "llvm/test/CodeGen/Haydn/phase-firewall-s96-anyext.ll"
    "llvm/test/CodeGen/Haydn/phase-firewall-hasfp-adjusts-stack.ll"
    "llvm/test/CodeGen/Haydn/phase-firewall-align-maxparcels.ll"
    "llvm/test/CodeGen/Haydn/c-e2e-runtime.ll"
    "llvm/test/CodeGen/Haydn/c-e2e-bundle-dump.ll"
    "llvm/test/CodeGen/Haydn/debug-info.ll"
    "llvm/test/CodeGen/Haydn/cfi-callee-saves.ll"
    "llvm/test/CodeGen/Haydn/cfi-cfa-object-offset.ll"
    "llvm/test/CodeGen/Haydn/cfi-fixup-enabled.ll"
    "llvm/test/CodeGen/Haydn/baremetal-startup.ll"
    "llvm/test/CodeGen/Haydn/product-ld-selfcheck.ll"
    "lld/test/ELF/haydn/product-ld-bind.s"
    "llvm/test/CodeGen/Haydn/product-runtime-artifact-seats.ll"
    "llvm/test/CodeGen/Haydn/runtime-toolchain-owner-pins.ll"
    "llvm/test/CodeGen/Haydn/product-abi-conformance.ll"
    "llvm/test/CodeGen/Haydn/runtime-artifact-seats-selfcheck.ll"
    "llvm/test/CodeGen/Haydn/ci-verdict-artifact-selfcheck.ll"
    "llvm/test/MC/Haydn/hostile-byte-surface.s"
    "llvm/test/MC/Haydn/eflags-e96-product-profile.s"
  )
  _lit_args=()
  for rel in "${_seats[@]}"; do
    if [[ -f "$LLVM_SRC/$rel" ]]; then _lit_args+=("$LLVM_SRC/$rel"); fi
  done
  if [[ ${#_lit_args[@]} -eq 0 ]]; then
    echo "  FAIL: no product evidence lit seats found" >&2; fail=1
  else
    _log=/tmp/haydn-product-coverage-lit.log
    if "$LIT" -sv "${_lit_args[@]}" >"$_log" 2>&1; then
      echo "  PASS: product evidence lit seats (${#_lit_args[@]})"
    else
      echo "  FAIL: product evidence lit seats" >&2
      if [[ -x "$SCRIPT_DIR/parse_lit_summary.py" ]]; then
        python3 "$SCRIPT_DIR/parse_lit_summary.py" "$_log" >&2 || true
      fi
      tail -40 "$_log" >&2 || true
      fail=1
    fi
  fi
else
  echo "  FAIL: llvm-lit not found under HAYDN_BIN" >&2; fail=1
fi

echo "======== product coverage pin done fail=$fail ========"
[[ "$fail" -eq 0 ]]
