#!/usr/bin/env bash
# check_runtime_artifact_seats.sh — advertised seat name (implementation is .py).
#
# Same-artifact compiler→sysroot→consumer identity. Does not rebuild libc,
# run BundleSim ctest, or execute CoreMark/Dhrystone.
set -euo pipefail
exec python3 "$(cd "$(dirname "$0")" && pwd)/check_runtime_artifact_seats.py" "$@"
