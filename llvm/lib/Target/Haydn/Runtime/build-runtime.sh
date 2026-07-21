#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# build-runtime.sh — build the Haydn baremetal runtime (libhaydn.a). -*- sh -*
#===----------------------------------------------------------------------===#
#
# Produces, for the Haydn target (haydn-unknown-elf):
# * crt0.o — startup (_start: SP setup, call main, halt)
# * libhaydn.a — static archive of:
# haydn-rt.o (integer div/mod libcalls)
# haydn-runtime-extras.o (mem*/atomics/abort/exit)
# the compiler-rt soft-float builtins, compiled IN-PLACE
# from compiler-rt/lib/builtins. These are the
# __addsf3/__adddf3/__mulsf3/... __fix*/__float* single
# AND double, conversions, and compare routines — the
# complete IEEE-754 soft-float surface the Haydn GISel
# legalizer already emits as libcalls.
#
# The compiler-rt fp sources are referenced from their upstream location; they
# are NOT vendored and NO upstream file is modified (CLAUDE.md). They are
# pure portable integer C over fp_lib.h/int_lib.h (Haydn is little-endian, so
# ENDIAN_LITTLE matches), plus fp_mode.c for the no-fenv __fe_getround
# __fe_raise_inexact stubs. See for the full rationale and the (codex
# debated) scope: copysign is bit-trick-lowered in the legalizer (no symbol);
# floor/ceil/rint/fmin/fmax are named libcalls left as clean libm link-errors.
#
# Usage:
# build-runtime.sh # uses $CLANG or default build clang
# CLANG=/path/bin/clang./build-runtime.sh
# build-runtime.sh --install # also copy into the clang resource dir
#
# Run AFTER `ninja` has built the Haydn-capable clang.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Runtime/ is llvm/lib/Target/Haydn/Runtime -> repo root (has compiler-rt) is
# 5 levels up: Haydn -> Target -> lib -> llvm -> <repo-root>.
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
CR_BUILTINS="$REPO_ROOT/compiler-rt/lib/builtins"

CLANG="${CLANG:-/ssd2/mhyang/haydn-build/bin/clang}"
LLVM_AR="${LLVM_AR:-$(dirname "$CLANG")/llvm-ar}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Resolve the install resource dir from the clang we are using.
RESOURCE_LIB="$("$CLANG" -target haydn-unknown-elf -print-resource-dir)/lib/haydn-unknown-unknown-elf"
INSTALL="${INSTALL:-$RESOURCE_LIB}"
DO_INSTALL=0
[[ "${1:-}" == "--install" ]] && DO_INSTALL=1

echo ">> clang:      $CLANG"
echo ">> builtins:   $CR_BUILTINS"
echo ">> work:       $WORK"
[[ "$DO_INSTALL" == 1 ]] && echo ">> install:    $INSTALL"

# NOTE: deliberately NOT -nostdinc: the runtime TUs (and compiler-rt's
# fp_lib.h/int_types.h) need clang's own freestanding <stdint.h>/<stddef.h>
# from the resource dir, which -ffreestanding keeps available. -fno-builtin
# stops clang from folding the __addsf3-style routines into builtins.
CFLAGS=(-target haydn-unknown-elf -O2 -ffreestanding
        "-I$CR_BUILTINS" -fno-builtin -fno-exceptions)

echo ">> compiling runtime objects"
"$CLANG" "${CFLAGS[@]}" -c "$SCRIPT_DIR/haydn-rt.c"          -o "$WORK/haydn-rt.o"
"$CLANG" "${CFLAGS[@]}" -c "$SCRIPT_DIR/haydn-runtime-extras.c" -o "$WORK/haydn-runtime-extras.o"

echo ">> compiling compiler-rt soft-float builtins (in-place, no upstream edit)"
# Single-precision arithmetic / conversions / compare.
SF_TUS=(addsf3 subsf3 mulsf3 divsf3 negsf2 comparesf2
        fixsfsi fixsfdi fixunssfsi fixunssfdi
        floatsisf floatdisf floatunsisf floatundisf
        extendsfdf2)
# Double-precision arithmetic / conversions / compare.
DF_TUS=(adddf3 subdf3 muldf3 divdf3 negdf2 comparedf2
        fixdfsi fixdfdi fixunsdfsi fixunsdfdi
        floatsidf floatdidf floatunsidf floatundidf
        truncdfsf2)
for tu in "${SF_TUS[@]}" "${DF_TUS[@]}" fp_mode; do
  "$CLANG" "${CFLAGS[@]}" -c "$CR_BUILTINS/$tu.c" -o "$WORK/cr_$tu.o"
done

echo ">> archiving libhaydn.a"
"$LLVM_AR" rcs "$SCRIPT_DIR/libhaydn.a" "$WORK"/*.o

echo ">> verifying libhaydn.a exports the soft-float surface (no undefined refs)"
# Every soft-float builtin must be defined; the archive must be self-contained
# (the only allowed undefined is none — float<->i64 resolves inside the set).
"$LLVM_AR" t "$SCRIPT_DIR/libhaydn.a" | sort | sed 's/^/   /'

if [[ "$DO_INSTALL" == 1 ]]; then
  echo ">> installing crt0.o, libhaydn.a, haydn.ld -> $INSTALL"
  mkdir -p "$INSTALL"
  "$CLANG" "${CFLAGS[@]}" -c "$SCRIPT_DIR/crt0.s" -o "$INSTALL/crt0.o"
  cp "$SCRIPT_DIR/libhaydn.a" "$INSTALL/libhaydn.a"
  cp "$SCRIPT_DIR/haydn.ld"   "$INSTALL/haydn.ld"
  echo ">> done. clang now auto-links libhaydn.a for haydn-unknown-elf."
else
  echo ">> (dry-run) built $SCRIPT_DIR/libhaydn.a. Re-run with --install to"
  echo "   place crt0.o + libhaydn.a + haydn.ld in the clang resource dir."
fi
