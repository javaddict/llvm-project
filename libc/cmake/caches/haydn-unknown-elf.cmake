# BundleSim / Haydn baremetal llvm-libc (Phase B).
# Vendor I/O + exit come from BundleSim BSP libbundlesim_plat.a (hostcall CSR).

set(CMAKE_SYSTEM_PROCESSOR haydn CACHE STRING "")
set(RUNTIMES_TARGET_TRIPLE "haydn-unknown-elf" CACHE STRING "")

include(${CMAKE_CURRENT_LIST_DIR}/baremetal_common.cmake)

# Prefer freestanding compile flags for the DSP baremetal profile.
#
# -fomit-frame-pointer: Haydn -O2 already defaults to -mframe-pointer=none;
# baremetal freestanding wants the extra GPR. AIE model: R12 is a normal
# allocatable caller-saved GPR (no free AT / no -mreserve-r12-at).
#
# Keep hwloops enabled (default). Do not paper over string/ZOL miscompiles
# with libc source rewrites — generic strlen+memcpy strcpy must work. Fix
# SMS/hwloop/post-inc instead if soft-peel or trip-count bugs reappear.
set(LIBC_COMPILE_OPTIONS_DEFAULT
    "-fomit-frame-pointer"
    CACHE STRING "Extra libc compile flags for Haydn baremetal" FORCE)
