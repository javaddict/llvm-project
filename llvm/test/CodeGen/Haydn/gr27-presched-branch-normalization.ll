; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s
;
; GR2.7 class 2 MIR/lit pin: pre-scheduler CFG-form normalization. The
; addPreSched2 LongBranchNormalize + BranchRelaxation seat (after
; HaydnExpandPseudos, before the S1 PostMachineScheduler) promotes every
; out-of-range branch site to the TERMINAL IN-BLOCK long form BEFORE the
; first packet commit — no trampoline MBB, no CFG creation. The far
; conditional here becomes: address materialization (LUI+ADDI32_W on the
; FAR block symbol), INVERTED near conditional to the layout-NEXT block,
; JALR_W to the far target (final terminator). The interim addPreEmitPass
; BR then has no far site left to promote, so the postcommit CFG-creation
; wall never fires. The generic MachineVerifier forbids non-terminators
; after the first terminator, which is exactly why the materialization
; precedes the near conditional.

@g = global [3000 x i32] zeroinitializer

define void @far_cond_promoted_presched(i32 %x) {
; CHECK-LABEL: far_cond_promoted_presched:
entry:
  %c = icmp eq i32 %x, 0
  br i1 %c, label %exit, label %pad
; Address materialization on the FAR (exit) block symbol, LUI first
; (golden HI12/LO20 pairing — ADDI32 before LUI reads an undefined LO12).
; CHECK: lui [[SCR:r[0-9]+]], .LBB0_2
; CHECK-NEXT: addi32 [[SCR]], [[SCR]], .LBB0_2
; Inverted near conditional to the layout-NEXT (pad) block: guards the
; fallthrough-adjacent edge so the JALR takes the far edge.
; CHECK-NEXT: beqz {{r[0-9]+}}, .LBB0_1
; CHECK-NEXT: jalr [[SCR]], [[SCR]], 0
; The far target is the EXIT, not the near pad.
; CHECK-NOT: jalr {{r[0-9]+}}, {{r[0-9]+}}, .LBB0_1
pad:
  %p = phi ptr [ @g, %entry ]
  %p0 = getelementptr i32, ptr %p, i32 2900
  store volatile i32 0, ptr %p0
  %p1 = getelementptr i32, ptr %p, i32 2870
  store volatile i32 1, ptr %p1
  %p2 = getelementptr i32, ptr %p, i32 2840
  store volatile i32 2, ptr %p2
  %p3 = getelementptr i32, ptr %p, i32 2810
  store volatile i32 3, ptr %p3
  %p4 = getelementptr i32, ptr %p, i32 2780
  store volatile i32 4, ptr %p4
  %p5 = getelementptr i32, ptr %p, i32 2750
  store volatile i32 5, ptr %p5
  %p6 = getelementptr i32, ptr %p, i32 2720
  store volatile i32 6, ptr %p6
  %p7 = getelementptr i32, ptr %p, i32 2690
  store volatile i32 7, ptr %p7
  %p8 = getelementptr i32, ptr %p, i32 2660
  store volatile i32 8, ptr %p8
  %p9 = getelementptr i32, ptr %p, i32 2630
  store volatile i32 9, ptr %p9
  %p10 = getelementptr i32, ptr %p, i32 2600
  store volatile i32 10, ptr %p10
  %p11 = getelementptr i32, ptr %p, i32 2570
  store volatile i32 11, ptr %p11
  %p12 = getelementptr i32, ptr %p, i32 2540
  store volatile i32 12, ptr %p12
  %p13 = getelementptr i32, ptr %p, i32 2510
  store volatile i32 13, ptr %p13
  %p14 = getelementptr i32, ptr %p, i32 2480
  store volatile i32 14, ptr %p14
  %p15 = getelementptr i32, ptr %p, i32 2450
  store volatile i32 15, ptr %p15
  %p16 = getelementptr i32, ptr %p, i32 2420
  store volatile i32 16, ptr %p16
  %p17 = getelementptr i32, ptr %p, i32 2390
  store volatile i32 17, ptr %p17
  %p18 = getelementptr i32, ptr %p, i32 2360
  store volatile i32 18, ptr %p18
  %p19 = getelementptr i32, ptr %p, i32 2330
  store volatile i32 19, ptr %p19
  %p20 = getelementptr i32, ptr %p, i32 2300
  store volatile i32 20, ptr %p20
  %p21 = getelementptr i32, ptr %p, i32 2270
  store volatile i32 21, ptr %p21
  %p22 = getelementptr i32, ptr %p, i32 2240
  store volatile i32 22, ptr %p22
  %p23 = getelementptr i32, ptr %p, i32 2210
  store volatile i32 23, ptr %p23
  %p24 = getelementptr i32, ptr %p, i32 2180
  store volatile i32 24, ptr %p24
  %p25 = getelementptr i32, ptr %p, i32 2150
  store volatile i32 25, ptr %p25
  %p26 = getelementptr i32, ptr %p, i32 2120
  store volatile i32 26, ptr %p26
  %p27 = getelementptr i32, ptr %p, i32 2090
  store volatile i32 27, ptr %p27
  %p28 = getelementptr i32, ptr %p, i32 2060
  store volatile i32 28, ptr %p28
  %p29 = getelementptr i32, ptr %p, i32 2030
  store volatile i32 29, ptr %p29
  %p30 = getelementptr i32, ptr %p, i32 2000
  store volatile i32 30, ptr %p30
  %p31 = getelementptr i32, ptr %p, i32 1970
  store volatile i32 31, ptr %p31
  %p32 = getelementptr i32, ptr %p, i32 1940
  store volatile i32 32, ptr %p32
  %p33 = getelementptr i32, ptr %p, i32 1910
  store volatile i32 33, ptr %p33
  %p34 = getelementptr i32, ptr %p, i32 1880
  store volatile i32 34, ptr %p34
  %p35 = getelementptr i32, ptr %p, i32 1850
  store volatile i32 35, ptr %p35
  %p36 = getelementptr i32, ptr %p, i32 1820
  store volatile i32 36, ptr %p36
  %p37 = getelementptr i32, ptr %p, i32 1790
  store volatile i32 37, ptr %p37
  %p38 = getelementptr i32, ptr %p, i32 1760
  store volatile i32 38, ptr %p38
  %p39 = getelementptr i32, ptr %p, i32 1730
  store volatile i32 39, ptr %p39
  %p40 = getelementptr i32, ptr %p, i32 1700
  store volatile i32 40, ptr %p40
  %p41 = getelementptr i32, ptr %p, i32 1670
  store volatile i32 41, ptr %p41
  %p42 = getelementptr i32, ptr %p, i32 1640
  store volatile i32 42, ptr %p42
  %p43 = getelementptr i32, ptr %p, i32 1610
  store volatile i32 43, ptr %p43
  %p44 = getelementptr i32, ptr %p, i32 1580
  store volatile i32 44, ptr %p44
  %p45 = getelementptr i32, ptr %p, i32 1550
  store volatile i32 45, ptr %p45
  %p46 = getelementptr i32, ptr %p, i32 1520
  store volatile i32 46, ptr %p46
  %p47 = getelementptr i32, ptr %p, i32 1490
  store volatile i32 47, ptr %p47
  %p48 = getelementptr i32, ptr %p, i32 1460
  store volatile i32 48, ptr %p48
  %p49 = getelementptr i32, ptr %p, i32 1430
  store volatile i32 49, ptr %p49
  %p50 = getelementptr i32, ptr %p, i32 1400
  store volatile i32 50, ptr %p50
  %p51 = getelementptr i32, ptr %p, i32 1370
  store volatile i32 51, ptr %p51
  %p52 = getelementptr i32, ptr %p, i32 1340
  store volatile i32 52, ptr %p52
  %p53 = getelementptr i32, ptr %p, i32 1310
  store volatile i32 53, ptr %p53
  %p54 = getelementptr i32, ptr %p, i32 1280
  store volatile i32 54, ptr %p54
  %p55 = getelementptr i32, ptr %p, i32 1250
  store volatile i32 55, ptr %p55
  %p56 = getelementptr i32, ptr %p, i32 1220
  store volatile i32 56, ptr %p56
  %p57 = getelementptr i32, ptr %p, i32 1190
  store volatile i32 57, ptr %p57
  %p58 = getelementptr i32, ptr %p, i32 1160
  store volatile i32 58, ptr %p58
  %p59 = getelementptr i32, ptr %p, i32 1130
  store volatile i32 59, ptr %p59
  %p60 = getelementptr i32, ptr %p, i32 1100
  store volatile i32 60, ptr %p60
  %p61 = getelementptr i32, ptr %p, i32 1070
  store volatile i32 61, ptr %p61
  %p62 = getelementptr i32, ptr %p, i32 1040
  store volatile i32 62, ptr %p62
  %p63 = getelementptr i32, ptr %p, i32 1010
  store volatile i32 63, ptr %p63
  %p64 = getelementptr i32, ptr %p, i32 980
  store volatile i32 64, ptr %p64
  %p65 = getelementptr i32, ptr %p, i32 950
  store volatile i32 65, ptr %p65
  %p66 = getelementptr i32, ptr %p, i32 920
  store volatile i32 66, ptr %p66
  %p67 = getelementptr i32, ptr %p, i32 890
  store volatile i32 67, ptr %p67
  %p68 = getelementptr i32, ptr %p, i32 860
  store volatile i32 68, ptr %p68
  %p69 = getelementptr i32, ptr %p, i32 830
  store volatile i32 69, ptr %p69
  %p70 = getelementptr i32, ptr %p, i32 800
  store volatile i32 70, ptr %p70
  %p71 = getelementptr i32, ptr %p, i32 770
  store volatile i32 71, ptr %p71
  %p72 = getelementptr i32, ptr %p, i32 740
  store volatile i32 72, ptr %p72
  %p73 = getelementptr i32, ptr %p, i32 710
  store volatile i32 73, ptr %p73
  %p74 = getelementptr i32, ptr %p, i32 680
  store volatile i32 74, ptr %p74
  %p75 = getelementptr i32, ptr %p, i32 650
  store volatile i32 75, ptr %p75
  %p76 = getelementptr i32, ptr %p, i32 620
  store volatile i32 76, ptr %p76
  %p77 = getelementptr i32, ptr %p, i32 590
  store volatile i32 77, ptr %p77
  %p78 = getelementptr i32, ptr %p, i32 560
  store volatile i32 78, ptr %p78
  %p79 = getelementptr i32, ptr %p, i32 530
  store volatile i32 79, ptr %p79
  br label %exit
exit:
  ret void
}
