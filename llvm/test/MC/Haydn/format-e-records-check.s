# RUN: %python %S/../../../lib/Target/Haydn/FormatE/generate_format_e_records.py --check
# REQUIRES: haydn-registered-target

# Role: generator-check — A0 fail-closed parity gate (NOT an object/encode
# contract). Committed Format E generated records (HaydnGenFormatERecords.inc,
# SetDesc ledger, E96 members TD, member opcodes) must match regeneration from
# the pinned golden JSON. No write path. Object encode→obj→disasm lives in
# sibling MC lits; do not label this --check as semantic/object qualification.
#
# Drift in TD/INC that would otherwise leave product encode/decode tables
# silently wrong fails this lit (Haydn CodeGen+MC product gate / A0).
