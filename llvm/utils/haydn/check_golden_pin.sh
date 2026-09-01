#!/usr/bin/env bash
# Monorepo golden-pin seat.
#
# Compiler FormatE pin is nine-file (xlsx by extracted cell digest).
# Catalog pin is six-file by design: unused/derived nine-file members stay
# on the compiler pin. Do not invent a seventh catalog input. Product
# catalog FILE sha256 is comment-sensitive (ARTIFACT hashes the pin file).
#
# Exit:
#   0   pin OK
#   1   pin FAIL
#   2   usage / missing tools
#   77  SKIP — live golden verify requested but golden dir unset
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
FAMILY_CORE="$ROOT/llvm/lib/Target/Haydn/FormatE/family_core.py"
COMPILER_PIN="$ROOT/llvm/lib/Target/Haydn/FormatE/GOLDEN_INPUTS.sha256"
CATALOG_PIN="$ROOT/simulator/bundlesim/isa/database/generated/GOLDEN_INPUTS.sha256"
FORMAT_E_DIR="$ROOT/llvm/lib/Target/Haydn/FormatE"

usage() {
  echo "usage: $0 [--self-test] [GOLDEN_DIR]" >&2
  exit 2
}

SELF_TEST=0
GOLDEN=""
for arg in "$@"; do
  case "$arg" in
    --self-test|--self-check) SELF_TEST=1 ;;
    -h|--help) usage ;;
    -*)
      echo "error: unknown flag $arg" >&2
      usage
      ;;
    *)
      if [[ -n "$GOLDEN" ]]; then
        echo "error: extra argument $arg" >&2
        usage
      fi
      GOLDEN="$arg"
      ;;
  esac
done

if [[ -z "$GOLDEN" ]]; then
  GOLDEN="${BUNDLESIM_GOLDEN_DIR:-${HAYDN_GOLDEN_DIR:-}}"
fi

fail() {
  echo "GOLDEN pin: FAIL $*" >&2
  exit 1
}

if [[ ! -f "$FAMILY_CORE" ]]; then
  fail "missing $FAMILY_CORE"
fi
if [[ ! -f "$COMPILER_PIN" ]]; then
  fail "missing compiler pin $COMPILER_PIN"
fi
if [[ ! -f "$CATALOG_PIN" ]]; then
  fail "missing catalog pin $CATALOG_PIN"
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "GOLDEN pin: FAIL python3 not found" >&2
  exit 2
fi
if ! command -v sha256sum >/dev/null 2>&1; then
  echo "GOLDEN pin: FAIL sha256sum not found" >&2
  exit 2
fi

# Catalog product FILE hash (six digest lines, no comments).
WANT_CATALOG_FILE_SHA="$(
  python3 - "$FAMILY_CORE" <<'PY'
import re, sys
from pathlib import Path
text = Path(sys.argv[1]).read_text(encoding="utf-8")
m = re.search(
    r'CATALOG_PIN_FILE_SHA256 = \(\s*\n\s*"([0-9a-f]{64})"',
    text,
)
if not m:
    raise SystemExit("error: CATALOG_PIN_FILE_SHA256 missing from family_core.py")
print(m.group(1))
PY
)"
GOT_CATALOG_FILE_SHA="$(sha256sum -- "$CATALOG_PIN" | awk '{print $1}')"
if [[ "$GOT_CATALOG_FILE_SHA" != "$WANT_CATALOG_FILE_SHA" ]]; then
  fail "catalog GOLDEN_INPUTS.sha256 file sha256 $GOT_CATALOG_FILE_SHA != product pin $WANT_CATALOG_FILE_SHA (comment/whitespace rewrites are content drift; keep six digest lines only)"
fi

# Six digest lines, no comments, v2_2 names only.
mapfile -t PIN_LINES < <(grep -v '^[[:space:]]*$' "$CATALOG_PIN" || true)
if [[ "${#PIN_LINES[@]}" -ne 6 ]]; then
  fail "catalog pin has ${#PIN_LINES[@]} rows; want six-file"
fi
for line in "${PIN_LINES[@]}"; do
  if [[ "$line" == \#* ]]; then
    fail "catalog pin has a comment line (FILE hash is product content)"
  fi
  if [[ "$line" == *format_e_bit_layout_v2.xlsx* && "$line" != *format_e_bit_layout_v2_2.xlsx* ]]; then
    fail "catalog pin names stale layout xlsx (want v2_2)"
  fi
  if [[ "$line" == *format_e_bit_layout_v2.json* && "$line" != *format_e_bit_layout_v2_2.json* ]]; then
    fail "catalog pin names stale layout json (want v2_2)"
  fi
done
if grep -E 'operands_info\.md|instruction_type_operands\.json|instruction_to_entry\.xlsx' "$CATALOG_PIN" >/dev/null; then
  fail "catalog pin lists unused/derived rows; those stay on the compiler pin"
fi

# Compiler pin must keep the remaining three (xlsx by cell digest).
if ! grep -E 'operands_info\.md' "$COMPILER_PIN" >/dev/null; then
  fail "compiler pin missing operands_info.md"
fi
if ! grep -E 'instruction_type_operands\.json' "$COMPILER_PIN" >/dev/null; then
  fail "compiler pin missing instruction_type_operands.json"
fi
if ! grep -E 'instruction_to_entry\.xlsx#cells' "$COMPILER_PIN" >/dev/null; then
  fail "compiler pin missing instruction_to_entry.xlsx#cells"
fi
if grep -E 'instruction_to_entry\.xlsx[[:space:]]*$' "$COMPILER_PIN" >/dev/null; then
  fail "compiler pin ZIP-byte-pins instruction_to_entry.xlsx (want #cells)"
fi

# Non-golden authority cites are not pin/script inputs.
if grep -nE 'hypo_encoding|encoding_manual|/Database/hypo' \
     "$FORMAT_E_DIR"/*.py "$COMPILER_PIN" "$CATALOG_PIN" >/dev/null; then
  grep -nE 'hypo_encoding|encoding_manual|/Database/hypo' \
    "$FORMAT_E_DIR"/*.py "$COMPILER_PIN" "$CATALOG_PIN" >&2 || true
  fail "non-golden authority citation on pin surface"
fi
echo "OK no non-golden authority cites"

python3 "$FAMILY_CORE" --pin-check
echo "OK check_golden_pin.sh self-test"

LIVE=0
if [[ "$SELF_TEST" -eq 0 && -n "$GOLDEN" ]]; then
  LIVE=1
elif [[ "${BUNDLESIM_REQUIRE_GOLDEN:-0}" == "1" && -z "$GOLDEN" ]]; then
  fail "BUNDLESIM_REQUIRE_GOLDEN=1 but golden dir unset"
elif [[ "$SELF_TEST" -eq 0 && -z "$GOLDEN" && "${REQUIRE_CATALOG_SIX_FILE:-0}" == "1" ]]; then
  echo "GOLDEN pin: SKIP (set BUNDLESIM_GOLDEN_DIR / HAYDN_GOLDEN_DIR or pass path)"
  exit 77
fi

if [[ "$LIVE" -eq 1 ]]; then
  if [[ ! -d "$GOLDEN" ]]; then
    fail "directory missing: $GOLDEN"
  fi
  if [[ ! -f "$GOLDEN/format_e_bit_layout_v2_2.json" && -f "$GOLDEN/golden/format_e_bit_layout_v2_2.json" ]]; then
    GOLDEN="$GOLDEN/golden"
  fi
  if [[ ! -f "$GOLDEN/format_e_bit_layout_v2_2.json" ]]; then
    fail "golden dir missing format_e_bit_layout_v2_2.json: $GOLDEN"
  fi
  (
    cd -- "$GOLDEN"
    if sha256sum -c "$CATALOG_PIN" --quiet; then
      echo "GOLDEN pin: OK catalog six-file ($GOLDEN)"
    else
      echo "GOLDEN pin: FAIL checksum mismatch under $GOLDEN" >&2
      sha256sum -c "$CATALOG_PIN" 2>&1 | tail -20 >&2 || true
      exit 1
    fi
  )
  export HAYDN_GOLDEN_DIR="$GOLDEN"
  python3 "$FAMILY_CORE" --check --family e96
  echo "GOLDEN pin: OK compiler nine-file + catalog six-file"
fi
exit 0
