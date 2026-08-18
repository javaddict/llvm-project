//===----------------------------------------------------------------------===//
// MAC FieldSlot per-op defs retired (R13). Logical opcodes live in Auto.td;
// product members are Format E. Residual occupancy Mask is S1|S2 (0x6).
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC GRR ops (4 ops x 3 slots = 12 defs).
// 3 GPR32 (rt, rs1, rs2)
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC D_R ops (5 ops x 3 slots = 15 defs).
// 2 DR64 (rtd, rsd)
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC D_RR ops (114 ops x 3 slots = 342 defs).
// 3 DR64 non-accum (rtd, rsd1, rsd2)
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC D_RR2 ops (7 ops x 3 slots = 21 defs).
// 4 DR64 non-accum 2W (rtd1, rtd2, rsd1, rsd2).
// (Path B): TRUE 2-output (golden DR_Write_Port=[rtd1,rtd2]). Revert the
// 1-dest collapse — bind BOTH dest fields so no orphan bits-field remains
// (every format-class field maps 1:1 to a dag operand, AIE-faithful). The
// selector now emits a 2-def MI; the encoder reads all 4 bound operands.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC D_RRA ops (219 ops x 3 slots = 657 defs).
// 3 DR64 accum (rtd, rsd1, rsd2) + rta tied=rtd (not encoded)
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC D_RRA2 ops (8 ops x 3 slots = 24 defs).
// 4 DR64 accum 2W (rtd1, rtd2, rsd1, rsd2) + rta1=rtd1, rta2=rtd2 ties.
// (Path B): TRUE 2-output accumulate (golden DR_Write_Port=[rtd1,rtd2]
// DR_Read_Port=[rtd1,rtd2,rsd1,rsd2]). Revert the 1-dest collapse — bind
// BOTH dest fields AND both accumulator inputs so no orphan bits-field remains
// (6 fields = 6 operands, AIE-faithful). The tied-def Constraints force
// regalloc to coalesce rta1->rtd1, rta2->rtd2 so silicon's accumulator read
// (from the encoded dest fields) sees the accumulator value (2-dest form).
//===----------------------------------------------------------------------===//


