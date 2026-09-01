# REQUIRES: haydn
# RUN: FileCheck %s --input-file=%S/../../../../llvm/lib/Target/Haydn/FormatE/GOLDEN_INPUTS.sha256 --check-prefix=GOLDEN
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r -h %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: llvm-readelf -r %t.o | FileCheck --check-prefix=ELFNUM %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000 --section-start=.rodata=0x20000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
# RUN: llvm-objdump -s -j .rodata %t | FileCheck --check-prefix=RODATA %s
# RUN: llvm-readobj --file-headers %t | FileCheck --check-prefix=LINKED %s
#
# Same-artifact pin vs the nine-file layout hashes: symbolic jalr is
# R_HAYDN_JALRSImm12 (ELF 22, rs+imm12, ValueShift=0), never the RI12
# branch row. Call-indirect jalr rd, rs, 0 bakes imm12=0 and must not mint
# a second JALR ELF kind. PIC/JT `.long ext_sym - jt` is R_HAYDN_32_PCREL.
# Objects keep EM_HAYDN=259 / EF_HAYDN_E96=0x1 (provisional; 259 collides
# with Kalray KVX — do not invent a replacement e_machine).
# Same-file global target at the next parcel so the patched imm12 is 12.
#
# GOLDEN-DAG: 2a2b43cb394a16cf89173538f2a673235fdb6e4ed04cb6e4e75e72520cf7ffdb  format_e_bit_layout_v2_2.xlsx
# GOLDEN-DAG: c436793cc8d3295088dda2271e68e5b53074eeb4bce3341d443ebac3e828dcba  format_e_bit_layout_v2_2.json
# GOLDEN-DAG: 741f5b4141990dc27dc217d2b0c0d7ab57240e08ef31c34bc036f11bbda1938e  format_e_canonical_vectors_v1.json
# GOLDEN-DAG: 3f306463d108c8240b6f8afa876e3fa4ca06b91ee7f38efe1fb4eba631c3433b  instruction_type_index.json
# GOLDEN-DAG: e4b61bf5b5be2634b1665474bf4906db0df017a939289a49d40e82bc2121fb12  operands_info.md
# GOLDEN-DAG: 434544ef336fe703ff69c6c59316c3e89f3350e1d4cae6fd6790929d01e7bd20  instruction_type_operands.json
# GOLDEN-DAG: ba6d65066b812083925ec5b68dae6dee26323ceb8aaab0040db0ff083f5d3098  instruction_to_entry.xlsx#cells
# GOLDEN-DAG: e0d7f7f0e7ce06622f4ae90dc9366caf16da48993c7f803c02d460473fd9b56a  VLIW_Engine_Compiler_Constraints.md
# GOLDEN-DAG: 550dac0c82c160397c510bd403116056e046a43d8df41cd34d81ab678cd9b49b  VLIW_Engine_Reference_Manual.docx
#
# RELOCS: Machine: 0x103
# RELOCS: Flags [ (0x1)
# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-NEXT:     0x0 R_HAYDN_JALRSImm12 ext_sym
# RELOCS-NEXT:   }
# RELOCS-NEXT:   Section ({{.*}}) .rela.rodata {
# RELOCS-NEXT:     0x0 R_HAYDN_32_PCREL ext_sym
# RELOCS-NEXT:     0x4 R_HAYDN_32_PCREL ext_sym
# RELOCS-NEXT:   }
# RELOCS-NEXT: ]
# RELOCS-NOT: R_HAYDN_WIDE_BranchSImm12
# RELOCS-NOT: R_HAYDN_WIDE_BranchSImm12_RI
# RELOCS-NOT: R_HAYDN_GOT
#
# ELF32 r_info low byte is the type: 0x16 = ELF 22.
# ELFNUM: {{[0-9a-fA-F]+}}16 R_HAYDN_JALRSImm12
# ELFNUM: R_HAYDN_32_PCREL
# ELFNUM-NOT: R_HAYDN_WIDE_BranchSImm12
#
# CHECK-LABEL: <_start>:
# CHECK: 10000: {{.*}} jalr{{.*}}r2, 12
# CHECK-LABEL: <ext_sym>:
# CHECK: {{.*}} add32
# CHECK-LABEL: <call_indirect>:
# CHECK: {{.*}} jalr{{.*}}r3, 0
#
# RODATA: Contents of section .rodata:
# RODATA: 20000 0c00ffff 0c00ffff
#
# LINKED: Machine: 0x103
# LINKED: Flags [ (0x1)
#
# Zero e_flags must fail closed — a 259 object without EF_HAYDN_E96 looks
# like a KVX-like reuse of 259. Do not invent a replacement e_machine.
# RUN: cp %t.o %t.zero.o
# RUN: %python -c "import struct,sys;f=open(sys.argv[1],'r+b');f.seek(36);f.write(struct.pack('<I',0));f.close()" %t.zero.o
# RUN: not ld.lld %t.zero.o -o %t.zero.out 2>&1 | FileCheck %s --check-prefix=REJECT0
# REJECT0: incompatible e_flags 0x0
# REJECT0: expected Format E ABI flag 0x1
# REJECT0: EM_HAYDN=259 experimental
# REJECT0: reuse 259 without this flag
#
# Wrong nonzero flag also rejects.
# RUN: cp %t.o %t.bad.o
# RUN: %python -c "import struct,sys;f=open(sys.argv[1],'r+b');f.seek(36);f.write(struct.pack('<I',2));f.close()" %t.bad.o
# RUN: not ld.lld %t.bad.o -o %t.bad.out 2>&1 | FileCheck %s --check-prefix=REJECT2
# REJECT2: incompatible e_flags 0x2
# REJECT2: expected Format E ABI flag 0x1

.section .text
.globl _start
_start:
    jalr r1, r2, ext_sym
    .size _start, .-_start

.globl ext_sym
ext_sym:
    { add32 r0, r0, r0 }
    .size ext_sym, .-ext_sym

.globl call_indirect
call_indirect:
    jalr_w lr, r3, 0
    .size call_indirect, .-call_indirect

.section .rodata
.globl jt
jt:
    .long ext_sym - jt
    .long ext_sym - jt
    .size jt, .-jt
