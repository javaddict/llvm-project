# RUN: %python %S/../../../lib/Target/Haydn/FormatE/family_core.py --check --family e96 | FileCheck %s
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/FormatE/GOLDEN_INPUTS.sha256 --check-prefix=PIN
# RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/FormatE/family_core.py --check-prefix=CORE
# REQUIRES: haydn-registered-target
# REQUIRES: haydn-golden-canonical
#
# Fail-closed nine-file golden pin on the compiler ledger. The three
# previously unpinned authority files live in FormatE/GOLDEN_INPUTS.sha256;
# --check refuses unpinned, derived, unused-as-input, unpublished,
# incomplete, and ZIP-byte xlsx pins. instruction_to_entry.xlsx is
# cell-pinned, not byte-pinned. Catalog generate_catalog.py keeps its own
# six-file ledger; unused/derived stay on this compiler pin.
#
# CHECK: OK matcher-root collapse
# CHECK: OK Manual.td tombstone
# CHECK: OK FormatsE96 tombstone
# CHECK: OK unknown family fail-closed
# CHECK: OK unpinned authority input fail-closed
# CHECK: OK derived instruction_to_entry.xlsx refused
# CHECK: OK unused authority input refused
# CHECK: OK unpublished encoding choice fail-closed
# CHECK: OK unpinned golden-dir file fail-closed
# CHECK: OK catalog pin refuses retired/unused rows
# CHECK: OK catalog pin six-file
# CHECK: OK incomplete compiler pin fail-closed
# CHECK: OK xlsx ZIP-byte pin rejected
# CHECK: OK authority pins files=9 pinned=8 consumed=5 unused=3 derived=1
#
# PIN-DAG: operands_info.md
# PIN-DAG: instruction_type_operands.json
# PIN-DAG: instruction_to_entry.xlsx#cells
# PIN-DAG: format_e_bit_layout_v2_2.json
# PIN-DAG: instruction_type_index.json
# PIN-DAG: not ZIP bytes
# PIN-NOT: slot0_alu_instruction_list.json
#
# CORE-DAG: catalog pin is six-file
# CORE-DAG: unused/derived stay on the compiler pin
# CORE-DAG: matcher root includes
# CORE-DAG: HaydnGeneric.td only
# CORE-DAG: MF0 family must stay inert
# CORE-DAG: HaydnFormatsE96.td must remain a 0-def tombstone
# CORE-DAG: unknown family
# CORE-NOT: FieldSlot
# CORE-NOT: hypo_encoding
