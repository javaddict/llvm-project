# RUN: bash %S/../../../utils/haydn/check_golden_pin.sh --self-test | FileCheck %s
# REQUIRES: haydn-registered-target
#
# Catalog GOLDEN_INPUTS.sha256 pins six of nine by design. Compiler FormatE
# pin is nine-file (xlsx by cell digest). Fail-closed consume: no seventh
# catalog input, no comment rewrite of the catalog pin FILE, no non-golden
# authority cites on the pin scripts.
#
# CHECK: OK no non-golden authority cites
# CHECK: OK catalog pin refuses retired/unused rows
# CHECK: OK catalog pin six-file
# CHECK: OK incomplete compiler pin fail-closed
# CHECK: OK xlsx ZIP-byte pin rejected
# CHECK: OK catalog pin file sha256
# CHECK: OK catalog pin comment rewrite is content drift
# CHECK: OK catalog pin refuses seventh input
# CHECK: OK catalog pin refuses invented seventh input
# CHECK: OK catalog pin on-disk six-file
# CHECK: OK compiler pin nine-file
# CHECK: OK check_golden_pin.sh self-test
# CHECK-NOT: seventh catalog input did not fail closed
