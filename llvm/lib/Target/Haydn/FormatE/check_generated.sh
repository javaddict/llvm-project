#!/usr/bin/env bash
# Umbrella generated-source qualification for admitted Format E family e96.
# Ordinary LLVM builds do not run this. Fail closed if golden inputs are missing.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../../.." && pwd)"
cd "$ROOT"
if [ -z "${HAYDN_GOLDEN_DIR:-}" ] && [ -z "${BUNDLESIM_GOLDEN_DIR:-}" ]; then
  echo "error: set HAYDN_GOLDEN_DIR or BUNDLESIM_GOLDEN_DIR to the pinned golden directory (no host-path fallback)" >&2
  exit 2
fi
python3 llvm/lib/Target/Haydn/FormatE/family_core.py --pin-check
python3 llvm/lib/Target/Haydn/FormatE/family_core.py --check --family e96
python3 llvm/lib/Target/Haydn/FormatE/generate_format_e_records.py --check --family e96
python3 llvm/lib/Target/Haydn/FormatE/generate_sched_records.py --check --family e96
echo "umbrella generated-source check passed (e96)"
