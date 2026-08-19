# RUN: env -u HAYDN_GOLDEN_DIR -u BUNDLESIM_GOLDEN_DIR not %python %S/../../../lib/Target/Haydn/FormatE/generate_format_e_records.py --check 2>&1 | FileCheck %s
# REQUIRES: haydn-registered-target
#
# P19: generators have no host-path golden fallback. Qualification fails
# closed when HAYDN_GOLDEN_DIR and BUNDLESIM_GOLDEN_DIR are unset.
#
# CHECK: set HAYDN_GOLDEN_DIR or BUNDLESIM_GOLDEN_DIR
# CHECK: no host-path fallback
