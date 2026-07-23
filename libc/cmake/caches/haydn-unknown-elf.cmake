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
# Hwloops left enabled (default). Global -hwloop was a blunt workaround for
# rare large-body uimm overflows (scanf/strtod_l) and miscompiled soft-loop
# string routines (memcpy only wrote 1 byte). Prefer FixupHwLoops / per-TU
# opts for oversized bodies — not a libc-wide feature kill.
set(LIBC_COMPILE_OPTIONS_DEFAULT
    "-fomit-frame-pointer"
    CACHE STRING "Extra libc compile flags for Haydn baremetal" FORCE)
