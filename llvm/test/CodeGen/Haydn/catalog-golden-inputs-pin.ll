; RUN: %python %S/../../../lib/Target/Haydn/FormatE/family_core.py --check --family e96 | FileCheck %s
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/FormatE/GOLDEN_INPUTS.sha256 --check-prefix=PIN
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/FormatE/family_core.py --check-prefix=CORE
; REQUIRES: haydn-registered-target
; REQUIRES: haydn-golden-canonical
;
; Compiler nine-file pin vs catalog six-file product pin. ARTIFACT.json
; hashes the catalog pin FILE, so header comments on that file are content
; drift (stamp 2af5e0ac…). Unused/derived rows stay on the compiler pin.
;
; CHECK: OK unpinned authority input fail-closed
; CHECK: OK derived instruction_to_entry.xlsx refused
; CHECK: OK unused authority input refused
; CHECK: OK unpublished encoding choice fail-closed
; CHECK: OK catalog pin refuses retired/unused rows
; CHECK: OK catalog pin six-file
; CHECK: OK incomplete compiler pin fail-closed
; CHECK: OK xlsx ZIP-byte pin rejected
; CHECK: OK catalog pin file sha256
; CHECK: OK catalog pin comment rewrite is content drift
; CHECK: OK authority pins files=9 pinned=8 consumed=5 unused=3 derived=1
;
; PIN-DAG: operands_info.md
; PIN-DAG: instruction_type_operands.json
; PIN-DAG: instruction_to_entry.xlsx#cells
; PIN-DAG: format_e_bit_layout_v2_2.json
; PIN-DAG: instruction_type_index.json
; PIN-DAG: not ZIP bytes
; PIN-NOT: slot0_alu_instruction_list.json
;
; CORE-DAG: catalog pin is six-file
; CORE-DAG: unused/derived stay on the compiler pin
; CORE-DAG: CATALOG_PIN_FILE_SHA256
; CORE-DAG: 2af5e0ac5f1ee9dae3b69a46df81c88b91fd78cdd914b0b6667bce2b6036c778
; CORE-DAG: Comment or whitespace rewrites are catalog content drift
; CORE-DAG: Keep six digest lines
; CORE-NOT: FieldSlot
