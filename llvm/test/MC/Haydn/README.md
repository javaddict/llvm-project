# Haydn MC Layer Tests

This directory contains MC (assembler/disassembler) layer tests for the Haydn DSP backend.

## Test Files

| File | Purpose |
|------|---------|
| `basic.s` | Core scalar ALU, load/store, branch, and jump instructions |
| `registers.s` | Register naming tests (GPR R0-R15, DR D0-D15, AR, SP/FP/LR aliases) |
| `encoding.s` | Binary encoding verification tests |
| `directives.s` | Assembler directives (.text, .data, .byte, .word, .global, etc.) |
| `fixups.s` | Relocation and fixup tests (branch targets, symbol references) |
| `invalid.s` | Invalid instruction/operand tests (error diagnostics) |
| `simd.s` | 64-bit ALU and SIMD instructions (Slot 1/2) |
| `comparisons.s` | Compare and conditional move instructions |
| `dsp-instructions.s` | DSP-specific operations (abs, neg, popcount, NSA, shifts) |
| `system-instructions.s` | CSR read/write, pseudo instructions, system ops |
| `comprehensive.s` | Complete instruction set coverage in one test |

## Running the Tests

Run all Haydn MC tests:
```bash
llvm-lit llvm/test/MC/Haydn/
```

Run a specific test with verbose output:
```bash
llvm-lit -vv llvm/test/MC/Haydn/basic.s
```

Run with show-encoding to verify binary encodings:
```bash
llvm-mc -triple=haydn-unknown-elf -show-encoding < test.s
```

Assemble to object and disassemble:
```bash
llvm-mc -triple=haydn-unknown-elf -filetype=obj < test.s | llvm-objdump -d -
```

## Test Conventions

1. **Dual RUN lines**: Each test has both assembly and disassembly checks
2. **Comments**: Use `# //` for test comments (not shown in output)
3. **Check prefixes**: `CHECK` for assembly, `OBJ` for objdump output
4. **Exact mnemonics**: All instruction names match `HaydnInstrInfo.td`
5. **Operand order**: Follows the ISA spec and TableGen definitions

## Instruction Categories Covered

- **Slot 0 ALU**: ADD32, SUB32, AND32, OR32, XOR32, shifts, moves
- **Slot 0 Load/Store**: LD32, ST32, LD16, ST16, LD8, ST8, LDU16, LDU8
- **Branch**: BEQ, BNE, BGE, BLT, BGEU, BLTU, BEQZ, BNEZ, BGEZ, BLTZ
- **Jump/Link**: JAL, JALR
- **Slot 1/2 ALU64**: ADD64, SUB64, AND64, OR64, XOR64, shifts
- **SIMD**: X2ADD32, X2SUB32, X2MUL32, X4ADD16, X4MUL16
- **MAC**: MULQ31, MACQ31, MULQ63, MAC32
- **DSP**: ABS32, NEG32, POPCOUNT32, NSA32, BREV32
- **System**: CSRR, CSRW, WFI, RET

## Notes

- Tests are golden-driven: binary encodings are generated from the
  authoritative Format E layout pair
  `~/haydn-plans/Database/golden/format_e_bit_layout_v2_1.{json,xlsx}`
  via the CodeGenFormat backend (D394); never hand-hypothesized
- Relocation kinds live in `HaydnRelocLayout` (single geometry authority;
  GE96-03 branch/call = byte PC+imm, hwloop imm <<2)
- Retired "Stream A1/B" and "MVB milestone" vocabulary: see STATUS.md
