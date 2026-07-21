# Haydn baremetal runtime library
#
# Builds crt0.o (startup) and libhaydn-rt.a (runtime stubs) for baremetal
# Haydn ELF targets. These provide:
# crt0.o: minimal _start entry point (SP setup, call main, halt loop)
# libhaydn-rt.a: soft-float and integer arithmetic libcalls
# haydn.ld: default linker script (64KB RAM at 0x00000000)

# The runtime is built as a standalone library for the Haydn target.
# It is NOT built as part of the normal LLVM build — it is intended to be
# compiled with the Haydn-targeted clang and archived manually or via a
# build script. This CMakeLists.txt is provided for documentation and
# potential future integration.

# NOTE: To build the runtime manually:
# clang -target haydn-unknown-elf -c crt0.s -o crt0.o
# clang -target haydn-unknown-elf -c haydn-rt.c -o haydn-rt.o
# llvm-ar rcs libhaydn-rt.a haydn-rt.o
#
# To link a program:
# clang -target haydn-unknown-elf -T haydn.ld crt0.o program.o -L. -lhaydn-rt -o program.elf
