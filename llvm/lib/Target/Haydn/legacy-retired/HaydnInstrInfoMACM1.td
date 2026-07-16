//===- HaydnInstrInfoMACM1.td - Mode-1 (1011) MAC variants -*- tblgen -*-===//

def X2MULA32_M1 : FmtM2_S1_MAC22<1, 1,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x2mula32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2MULS32_M1 : FmtM2_S1_MAC22<1, 2,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x2muls32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4MULA16_M1 : FmtM2_S1_MAC22<3, 1,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4mula16\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4MULS16_M1 : FmtM2_S1_MAC22<3, 2,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4muls16\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4MULA16S_M1 : FmtM2_S1_MAC22<3, 3,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4mula16s\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4MULS16S_M1 : FmtM2_S1_MAC22<3, 4,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4muls16s\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4FF2MULA16S_M1 : FmtM2_S1_MAC22<3, 11,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4ff2mula16s\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4FF2MULS16S_M1 : FmtM2_S1_MAC22<3, 12,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2, DR64:$rd_in),
    "x4ff2muls16s\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2CMUL32_M1 : FmtM2_S1_MAC22<2, 0,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2CMUL32_F2_M1 : FmtM2_S1_MAC22<2, 1,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32_f2\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2CMUL32S_M1 : FmtM2_S1_MAC22<2, 2,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32s\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2CMUL32S_F2_M1 : FmtM2_S1_MAC22<2, 3,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32s_f2\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4FF2MUL16S_M1 : FmtM2_S1_MAC22<3, 10,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4ff2mul16s\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2MUL32_M1 : FmtM2_S1_MAC22<1, 0,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mul32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2MULAPL32_M1 : FmtM2_S1_MAC22<1, 14,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2mulapl32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2MULSPL32_M1 : FmtM2_S1_MAC22<1, 15,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2mulspl32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2MULAPH32_M1 : FmtM2_S1_MAC22<1, 17,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2mulaph32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2MULSPH32_M1 : FmtM2_S1_MAC22<1, 18,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2mulsph32\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FMULA32RS_M1 : FmtM2_S1_MAC22<1, 6,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fmula32rs\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FMULA32RSS_M1 : FmtM2_S1_MAC22<1, 7,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fmula32rss\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FMULA32TS_M1 : FmtM2_S1_MAC22<1, 11,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fmula32ts\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FCMULA32RS_M1 : FmtM2_S1_MAC22<2, 6,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fcmula32rs\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FCMULA32RSS_M1 : FmtM2_S1_MAC22<2, 7,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x2fcmula32rss\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4FCMULA16RS_M1 : FmtM2_S1_MAC22<2, 14,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4fcmula16rs\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4FCMULA16RSS_M1 : FmtM2_S1_MAC22<2, 15,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4fcmula16rss\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4CMULA16S_H_M1 : FmtM2_S1_MAC22<2, 18,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4cmula16s_h\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4CMULA16S_L_M1 : FmtM2_S1_MAC22<2, 19,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4cmula16s_l\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4CJMULA16S_H_M1 : FmtM2_S1_MAC22<2, 22,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4cjmula16s_h\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4CJMULA16S_L_M1 : FmtM2_S1_MAC22<2, 23,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1, DR64:$rsd2),
    "x4cjmula16s_l\t$rtd, $rsd1, $rsd2", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}



def X2DOT32_M1 : FmtM2_S1_MAC22<1, 3,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2dot32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2MULPL32_M1 : FmtM2_S1_MAC22<1, 13,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulpl32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2MULPH32_M1 : FmtM2_S1_MAC22<1, 16,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulph32\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FMUL32RS_M1 : FmtM2_S1_MAC22<1, 4,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FMUL32RSS_M1 : FmtM2_S1_MAC22<1, 5,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FMUL32TS_M1 : FmtM2_S1_MAC22<1, 10,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32ts\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FMULS32RS_M1 : FmtM2_S1_MAC22<1, 8,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FMULS32RSS_M1 : FmtM2_S1_MAC22<1, 9,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FMULS32TS_M1 : FmtM2_S1_MAC22<1, 12,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32ts\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FCMUL32RS_M1 : FmtM2_S1_MAC22<2, 4,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fcmul32rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X2FCMUL32RSS_M1 : FmtM2_S1_MAC22<2, 5,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fcmul32rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4CMUL16S_H_M1 : FmtM2_S1_MAC22<2, 16,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16s_h\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4CMUL16S_L_M1 : FmtM2_S1_MAC22<2, 17,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16s_l\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4CJMUL16S_H_M1 : FmtM2_S1_MAC22<2, 20,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmul16s_h\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4CJMUL16S_L_M1 : FmtM2_S1_MAC22<2, 21,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmul16s_l\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4FCMUL16RS_M1 : FmtM2_S1_MAC22<2, 12,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fcmul16rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4FCMUL16RSS_M1 : FmtM2_S1_MAC22<2, 13,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fcmul16rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4FMUL16TS_M1 : FmtM2_S1_MAC22<3, 7,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16ts\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4FMUL16RS_M1 : FmtM2_S1_MAC22<3, 8,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16rs\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4FMUL16RSS_M1 : FmtM2_S1_MAC22<3, 9,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16rss\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4MUL16_M1 : FmtM2_S1_MAC22<3, 0,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4mul16\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4DOT16_M1 : FmtM2_S1_MAC22<3, 5,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4dot16\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4CMUL16_M1 : FmtM2_S1_MAC22<2, 8,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16\t$rtd, $rsd1", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4CMUL16S_M1 : FmtM2_S1_MAC22<2, 9,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16s\t$rtd, $rsd1", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4CMUL16_F2_M1 : FmtM2_S1_MAC22<2, 10,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16_f2\t$rtd, $rsd1", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4CMUL16S_F2_M1 : FmtM2_S1_MAC22<2, 11,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16s_f2\t$rtd, $rsd1", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def X4ENERGY16_M1 : FmtM2_S1_MAC22<3, 6,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4energy16\t$rtd, $rsd1", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}


//=== MUL64 family: F2 class 0, variants 0x01-0x10, 1:1 no sub ===//
def MUL64_LL_M1 : FmtM2_S1_MAC22<0, 1,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_ll\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_LH_M1 : FmtM2_S1_MAC22<0, 2,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_lh\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_HL_M1 : FmtM2_S1_MAC22<0, 3,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_hl\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_HH_M1 : FmtM2_S1_MAC22<0, 4,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_hh\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_ULL_M1 : FmtM2_S1_MAC22<0, 5,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_ull\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_ULH_M1 : FmtM2_S1_MAC22<0, 6,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_ulh\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_UHL_M1 : FmtM2_S1_MAC22<0, 7,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_uhl\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_UHH_M1 : FmtM2_S1_MAC22<0, 8,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_uhh\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_LUL_M1 : FmtM2_S1_MAC22<0, 9,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_lul\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_LUH_M1 : FmtM2_S1_MAC22<0, 10,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_luh\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_ULUL_M1 : FmtM2_S1_MAC22<0, 11,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_ulul\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_HUH_M1 : FmtM2_S1_MAC22<0, 12,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_huh\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_HUL_M1 : FmtM2_S1_MAC22<0, 13,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_hul\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_ULUH_M1 : FmtM2_S1_MAC22<0, 14,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_uluh\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_UHUH_M1 : FmtM2_S1_MAC22<0, 15,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_uhuh\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

def MUL64_UHUL_M1 : FmtM2_S1_MAC22<0, 16,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64_uhul\t$rtd, $rsd1, $rsd2", []> {
  let DecoderNamespace = "HaydnM1S1MAC";
  let isPseudo = 0;
}

