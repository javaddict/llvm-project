# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s | llvm-objdump -d --triple=haydn-unknown-elf - | FileCheck --check-prefix=ROUNDTRIP %s
# REQUIRES: haydn-registered-target
// CHECK: { add64s d0, d1, d2 } // encoding: [0x07,0x8b,0x04,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { add64s_h d3, d4, d5 } // encoding: [0x07,0xcb,0x34,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { add64s_l d6, d7, d8 } // encoding: [0x07,0xab,0x64,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { sub64s d0, d1, d2 } // encoding: [0x07,0x8b,0x05,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { sub64s_h d3, d4, d5 } // encoding: [0x07,0xcb,0x35,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { sub64s_l d6, d7, d8 } // encoding: [0x07,0xab,0x65,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { add64_h d0, d1, d2 } // encoding: [0x07,0x4b,0x04,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { add64_l d3, d4, d5 } // encoding: [0x07,0x2b,0x34,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { sub64_h d6, d7, d8 } // encoding: [0x07,0x4b,0x65,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { sub64_l d9, d10, d11 } // encoding: [0x07,0x2b,0x95,0xba,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2add32s d0, d1, d2 } // encoding: [0x07,0x4b,0x08,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2sub32s d3, d4, d5 } // encoding: [0x07,0xcb,0x39,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2add32s_hllh d6, d7, d8 } // encoding: [0x07,0x6b,0x68,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2sub32s_hllh d9, d10, d11 } // encoding: [0x07,0xeb,0x99,0xba,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2add32_hllh d0, d1, d2 } // encoding: [0x07,0x2b,0x08,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2sub32_hllh d3, d4, d5 } // encoding: [0x07,0xab,0x39,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2addsub32 d0, d1, d2 } // encoding: [0x07,0x8b,0x08,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2addsub32s d3, d4, d5 } // encoding: [0x07,0xcb,0x38,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2subadd32 d6, d7, d8 } // encoding: [0x07,0x0b,0x69,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2subadd32s d9, d10, d11 } // encoding: [0x07,0x4b,0x99,0xba,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2clamp32 d0, d1, d2 } // encoding: [0x07,0xcb,0x0a,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4clamp16 d3, d4, d5 } // encoding: [0x07,0xcb,0x3c,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { max64 d0, d1, d2 } // encoding: [0x07,0x0b,0x07,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { min64 d3, d4, d5 } // encoding: [0x07,0x2b,0x37,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2max32 d0, d1, d2 } // encoding: [0x07,0x8b,0x0a,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2min32 d3, d4, d5 } // encoding: [0x07,0xab,0x3a,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4max16 d6, d7, d8 } // encoding: [0x07,0x8b,0x6c,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4min16 d9, d10, d11 } // encoding: [0x07,0xab,0x9c,0xba,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2dot32 d0, d1, d2 } // encoding: [0x47,0x81,0x0c,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4dot16 d3, d4, d5 } // encoding: [0x47,0x01,0x36,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2fcmul32rs d0, d1, d2 } // encoding: [0x47,0xb1,0x0c,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2fcmul32rss d3, d4, d5 } // encoding: [0x47,0xb9,0x3c,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4fcmul16rs d0, d1, d2 } // encoding: [0x47,0x21,0x06,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4fcmul16rss d3, d4, d5 } // encoding: [0x47,0x29,0x36,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2fmul32rs d0, d1, d2 } // encoding: [0x47,0x99,0x0c,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2fmul32rss d3, d4, d5 } // encoding: [0x47,0xa1,0x3c,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2fmul32ts d6, d7, d8 } // encoding: [0x47,0xa9,0x6c,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4fmul16rs d0, d1, d2 } // encoding: [0x47,0x11,0x06,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4fmul16rss d3, d4, d5 } // encoding: [0x47,0x19,0x36,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4fmul16ts d6, d7, d8 } // encoding: [0x47,0x09,0x66,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2sel32_hh d0, d1, d2 } // encoding: [0x07,0x6b,0x0b,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2sel32_hl d3, d4, d5 } // encoding: [0x07,0x4b,0x3b,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2sel32_lh d6, d7, d8 } // encoding: [0x07,0x2b,0x6b,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2sel32_ll d9, d10, d11 } // encoding: [0x07,0x0b,0x9b,0xba,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2mulaph32 d0, d1, d2 } // encoding: [0x47,0x39,0x0d,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2mulapl32 d3, d4, d5 } // encoding: [0x47,0x31,0x3d,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2mulsph32 d6, d7, d8 } // encoding: [0x47,0x49,0x6d,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2mulspl32 d9, d10, d11 } // encoding: [0x47,0x41,0x9d,0xba,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4add16s d0, d1, d2 } // encoding: [0x07,0x2b,0x0c,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4sub16s d3, d4, d5 } // encoding: [0x07,0x6b,0x3c,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2frsst32 d0, d1, d2 } // encoding: [0x07,0x8b,0x0b,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2frst32 d3, d4, d5 } // encoding: [0x07,0xab,0x3b,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4frsst16 d0, d1, d2 } // encoding: [0x07,0x8b,0x0d,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4frst16 d3, d4, d5 } // encoding: [0x07,0xab,0x3d,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4sat32t16 d0, d1, d2 } // encoding: [0x07,0xcb,0x0d,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4cjmul16s_h d0, d1, d2 } // encoding: [0x47,0xa1,0x06,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4cjmul16s_l d3, d4, d5 } // encoding: [0x47,0xa9,0x36,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4cmul16s_h d6, d7, d8 } // encoding: [0x47,0x81,0x66,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4cmul16s_l d9, d10, d11 } // encoding: [0x47,0x89,0x96,0xba,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2fmula32rs d0, d1, d2 } // encoding: [0x47,0x01,0x0d,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2fmula32rss d3, d4, d5 } // encoding: [0x47,0x09,0x3d,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2fmuls32rs d6, d7, d8 } // encoding: [0x47,0x19,0x6d,0x87,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2fmuls32rss d9, d10, d11 } // encoding: [0x47,0x21,0x9d,0xba,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2fcmula32rs d0, d1, d2 } // encoding: [0x47,0x51,0x0d,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x2fcmula32rss d3, d4, d5 } // encoding: [0x47,0x59,0x3d,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4ff2mul16s d4, d5, d6, d7 } // encoding: [0x47,0x02,0x5d,0x07,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4ff2mula16s d0, d1, d2, d3 } // encoding: [0x47,0x02,0x1e,0x03,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: { x4ff2muls16s d4, d5, d6, d7 } // encoding: [0x47,0x02,0x5f,0x07,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// ROUNDTRIP: {{.*}}0: 07 8b 04 21 00 00 00 00 00 00 00 00 { add64s d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}c: 07 cb 34 54 00 00 00 00 00 00 00 00 { add64s_h d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}18: 07 ab 64 87 00 00 00 00 00 00 00 00 { add64s_l d6, d7, d8; nop }
// ROUNDTRIP: {{.*}}24: 07 8b 05 21 00 00 00 00 00 00 00 00 { sub64s d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}30: 07 cb 35 54 00 00 00 00 00 00 00 00 { sub64s_h d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}3c: 07 ab 65 87 00 00 00 00 00 00 00 00 { sub64s_l d6, d7, d8; nop }
// ROUNDTRIP: {{.*}}48: 07 4b 04 21 00 00 00 00 00 00 00 00 { add64_h d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}54: 07 2b 34 54 00 00 00 00 00 00 00 00 { add64_l d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}60: 07 4b 65 87 00 00 00 00 00 00 00 00 { sub64_h d6, d7, d8; nop }
// ROUNDTRIP: {{.*}}6c: 07 2b 95 ba 00 00 00 00 00 00 00 00 { sub64_l d9, d10, d11; nop }
// ROUNDTRIP: {{.*}}78: 07 4b 08 21 00 00 00 00 00 00 00 00 { x2add32s d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}84: 07 cb 39 54 00 00 00 00 00 00 00 00 { x2sub32s d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}90: 07 6b 68 87 00 00 00 00 00 00 00 00 { x2add32s_hllh d6, d7, d8; nop }
// ROUNDTRIP: {{.*}}9c: 07 eb 99 ba 00 00 00 00 00 00 00 00 { x2sub32s_hllh d9, d10, d11; nop }
// ROUNDTRIP: {{.*}}a8: 07 2b 08 21 00 00 00 00 00 00 00 00 { x2add32_hllh d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}b4: 07 ab 39 54 00 00 00 00 00 00 00 00 { x2sub32_hllh d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}c0: 07 8b 08 21 00 00 00 00 00 00 00 00 { x2addsub32 d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}cc: 07 cb 38 54 00 00 00 00 00 00 00 00 { x2addsub32s d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}d8: 07 0b 69 87 00 00 00 00 00 00 00 00 { x2subadd32 d6, d7, d8; nop }
// ROUNDTRIP: {{.*}}e4: 07 4b 99 ba 00 00 00 00 00 00 00 00 { x2subadd32s d9, d10, d11; nop }
// ROUNDTRIP: {{.*}}f0: 07 cb 0a 21 00 00 00 00 00 00 00 00 { x2clamp32 d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}fc: 07 cb 3c 54 00 00 00 00 00 00 00 00 { x4clamp16 d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}108: 07 0b 07 21 00 00 00 00 00 00 00 00 { max64 d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}114: 07 2b 37 54 00 00 00 00 00 00 00 00 { min64 d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}120: 07 8b 0a 21 00 00 00 00 00 00 00 00 { x2max32 d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}12c: 07 ab 3a 54 00 00 00 00 00 00 00 00 { x2min32 d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}138: 07 8b 6c 87 00 00 00 00 00 00 00 00 { x4max16 d6, d7, d8; nop }
// ROUNDTRIP: {{.*}}144: 07 ab 9c ba 00 00 00 00 00 00 00 00 { x4min16 d9, d10, d11; nop }
// ROUNDTRIP: {{.*}}150: 47 81 0c 21 00 00 00 00 00 00 00 00 { x2dot32 d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}15c: 47 01 36 54 00 00 00 00 00 00 00 00 { x4dot16 d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}168: 47 b1 0c 21 00 00 00 00 00 00 00 00 { x2fcmul32rs d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}174: 47 b9 3c 54 00 00 00 00 00 00 00 00 { x2fcmul32rss d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}180: 47 21 06 21 00 00 00 00 00 00 00 00 { x4fcmul16rs d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}18c: 47 29 36 54 00 00 00 00 00 00 00 00 { x4fcmul16rss d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}198: 47 99 0c 21 00 00 00 00 00 00 00 00 { x2fmul32rs d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}1a4: 47 a1 3c 54 00 00 00 00 00 00 00 00 { x2fmul32rss d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}1b0: 47 a9 6c 87 00 00 00 00 00 00 00 00 { x2fmul32ts d6, d7, d8; nop }
// ROUNDTRIP: {{.*}}1bc: 47 11 06 21 00 00 00 00 00 00 00 00 { x4fmul16rs d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}1c8: 47 19 36 54 00 00 00 00 00 00 00 00 { x4fmul16rss d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}1d4: 47 09 66 87 00 00 00 00 00 00 00 00 { x4fmul16ts d6, d7, d8; nop }
// ROUNDTRIP: {{.*}}1e0: 07 6b 0b 21 00 00 00 00 00 00 00 00 { x2sel32_hh d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}1ec: 07 4b 3b 54 00 00 00 00 00 00 00 00 { x2sel32_hl d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}1f8: 07 2b 6b 87 00 00 00 00 00 00 00 00 { x2sel32_lh d6, d7, d8; nop }
// ROUNDTRIP: {{.*}}204: 07 0b 9b ba 00 00 00 00 00 00 00 00 { x2sel32_ll d9, d10, d11; nop }
// ROUNDTRIP: {{.*}}210: 47 39 0d 21 00 00 00 00 00 00 00 00 { x2mulaph32 d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}21c: 47 31 3d 54 00 00 00 00 00 00 00 00 { x2mulapl32 d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}228: 47 49 6d 87 00 00 00 00 00 00 00 00 { x2mulsph32 d6, d7, d8; nop }
// ROUNDTRIP: {{.*}}234: 47 41 9d ba 00 00 00 00 00 00 00 00 { x2mulspl32 d9, d10, d11; nop }
// ROUNDTRIP: {{.*}}240: 07 2b 0c 21 00 00 00 00 00 00 00 00 { x4add16s d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}24c: 07 6b 3c 54 00 00 00 00 00 00 00 00 { x4sub16s d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}258: 07 8b 0b 21 00 00 00 00 00 00 00 00 { x2frsst32 d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}264: 07 ab 3b 54 00 00 00 00 00 00 00 00 { x2frst32 d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}270: 07 8b 0d 21 00 00 00 00 00 00 00 00 { x4frsst16 d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}27c: 07 ab 3d 54 00 00 00 00 00 00 00 00 { x4frst16 d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}288: 07 cb 0d 21 00 00 00 00 00 00 00 00 { x4sat32t16 d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}294: 47 a1 06 21 00 00 00 00 00 00 00 00 { x4cjmul16s_h d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}2a0: 47 a9 36 54 00 00 00 00 00 00 00 00 { x4cjmul16s_l d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}2ac: 47 81 66 87 00 00 00 00 00 00 00 00 { x4cmul16s_h d6, d7, d8; nop }
// ROUNDTRIP: {{.*}}2b8: 47 89 96 ba 00 00 00 00 00 00 00 00 { x4cmul16s_l d9, d10, d11; nop }
// ROUNDTRIP: {{.*}}2c4: 47 01 0d 21 00 00 00 00 00 00 00 00 { x2fmula32rs d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}2d0: 47 09 3d 54 00 00 00 00 00 00 00 00 { x2fmula32rss d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}2dc: 47 19 6d 87 00 00 00 00 00 00 00 00 { x2fmuls32rs d6, d7, d8; nop }
// ROUNDTRIP: {{.*}}2e8: 47 21 9d ba 00 00 00 00 00 00 00 00 { x2fmuls32rss d9, d10, d11; nop }
// ROUNDTRIP: {{.*}}2f4: 47 51 0d 21 00 00 00 00 00 00 00 00 { x2fcmula32rs d0, d1, d2; nop }
// ROUNDTRIP: {{.*}}300: 47 59 3d 54 00 00 00 00 00 00 00 00 { x2fcmula32rss d3, d4, d5; nop }
// ROUNDTRIP: {{.*}}30c: 47 02 5d 07 06 00 00 00 00 00 00 00 { x4ff2mul16s d5, d6, d7; nop }
// ROUNDTRIP: {{.*}}318: 47 02 1e 03 02 00 00 00 00 00 00 00 { x4ff2mula16s d1, d2, d3; nop }
// ROUNDTRIP: {{.*}}324: 47 02 5f 07 06 00 00 00 00 00 00 00 { x4ff2muls16s d5, d6, d7; nop }

# Role: object — SIMD ALU instruction round-trip.

# SIMD ALU instruction round-trip.
# `{ nop; <op>.sN...; nop }` form. ALU64 ops route to slot 1 (``). The
# earlier ADD64_H/ADD64_L 8-byte-window decoder gap (which emitted `<?>`
# placeholders) was fixed by the single Format E FlexMap slot-authority
# cutover; both halves of this test now pass.
# SIMD ALU instruction test for Slot 1/2 DR64 datapath operations.
# Covers the full range of SIMD arithmetic from HaydnInstrInfoAuto.td:
# Saturating add/sub: ADD64S, SUB64S, X2ADD32S, X2SUB32S, X4ADD16S, X4SUB16S
# Halfword variants: ADD64_H, ADD64_L, SUB64_H, SUB64_L, etc.
# AddSub/SubAdd: X2ADDSUB32, X2SUBADD32
# Clamp: X2CLAMP32, X4CLAMP16
# Min/Max: X2MAX32, X2MIN32, X4MAX16, X4MIN16, MAX64, MIN64
# Dot product: X2DOT32, X4DOT16
# Complex multiply: X2FCMUL32RS, X4FCMUL16RS
# Fractional multiply: X2FMUL32RS, X4FMUL16RS
# Select: X2SEL32_HH/HL/LH/LL
# Pack high/low: X2MULAPH32, X2MULAPL32
# Round shift: X2FRSST32, X2FRST32, X4FRSST16, X4FRST16

#===----------------------------------------------------------------------===
# Saturating 64-bit Add/Sub
#===----------------------------------------------------------------------===


add64s d0, d1, d2

add64s_h d3, d4, d5

add64s_l d6, d7, d8

sub64s d0, d1, d2

sub64s_h d3, d4, d5

sub64s_l d6, d7, d8

#===----------------------------------------------------------------------===
# Non-saturating halfword Add/Sub
#===----------------------------------------------------------------------===

add64_h d0, d1, d2

add64_l d3, d4, d5

sub64_h d6, d7, d8

sub64_l d9, d10, d11

#===----------------------------------------------------------------------===
# SIMD X2 Saturating (dual 32-bit)
#===----------------------------------------------------------------------===

x2add32s d0, d1, d2

x2sub32s d3, d4, d5

x2add32s_hllh d6, d7, d8

x2sub32s_hllh d9, d10, d11

#===----------------------------------------------------------------------===
# SIMD X2 Non-saturating HLLH variants
#===----------------------------------------------------------------------===

x2add32_hllh d0, d1, d2

x2sub32_hllh d3, d4, d5

#===----------------------------------------------------------------------===
# AddSub / SubAdd
#===----------------------------------------------------------------------===

x2addsub32 d0, d1, d2

x2addsub32s d3, d4, d5

x2subadd32 d6, d7, d8

x2subadd32s d9, d10, d11

#===----------------------------------------------------------------------===
# Clamp
#===----------------------------------------------------------------------===

x2clamp32 d0, d1, d2

x4clamp16 d3, d4, d5

#===----------------------------------------------------------------------===
# Min / Max (SIMD and 64-bit scalar)
#===----------------------------------------------------------------------===

max64 d0, d1, d2

min64 d3, d4, d5

x2max32 d0, d1, d2

x2min32 d3, d4, d5

x4max16 d6, d7, d8

x4min16 d9, d10, d11

#===----------------------------------------------------------------------===
# Dot Product
#===----------------------------------------------------------------------===

x2dot32 d0, d1, d2

x4dot16 d3, d4, d5

#===----------------------------------------------------------------------===
# Complex Multiply (non-ternary, FmtALU64 forms)
#===----------------------------------------------------------------------===

x2fcmul32rs d0, d1, d2

x2fcmul32rss d3, d4, d5

x4fcmul16rs d0, d1, d2

x4fcmul16rss d3, d4, d5

#===----------------------------------------------------------------------===
# Fractional Multiply (non-ternary, FmtALU64 forms)
#===----------------------------------------------------------------------===

x2fmul32rs d0, d1, d2

x2fmul32rss d3, d4, d5

x2fmul32ts d6, d7, d8

x4fmul16rs d0, d1, d2

x4fmul16rss d3, d4, d5

x4fmul16ts d6, d7, d8

#===----------------------------------------------------------------------===
# Select
#===----------------------------------------------------------------------===

x2sel32_hh d0, d1, d2

x2sel32_hl d3, d4, d5

x2sel32_lh d6, d7, d8

x2sel32_ll d9, d10, d11

#===----------------------------------------------------------------------===
# Pack High/Low
#===----------------------------------------------------------------------===

x2mulaph32 d0, d1, d2

x2mulapl32 d3, d4, d5

x2mulsph32 d6, d7, d8

x2mulspl32 d9, d10, d11

#===----------------------------------------------------------------------===
# SIMD X4 Saturating (quad 16-bit)
#===----------------------------------------------------------------------===

x4add16s d0, d1, d2

x4sub16s d3, d4, d5

#===----------------------------------------------------------------------===
# Round Shift
#===----------------------------------------------------------------------===

x2frsst32 d0, d1, d2

x2frst32 d3, d4, d5

x4frsst16 d0, d1, d2

x4frst16 d3, d4, d5

#===----------------------------------------------------------------------===
# Saturation
#===----------------------------------------------------------------------===

x4sat32t16 d0, d1, d2

#===----------------------------------------------------------------------===
# SIMD X4 Complex Multiply (non-ternary forms)
#===----------------------------------------------------------------------===

x4cjmul16s_h d0, d1, d2

x4cjmul16s_l d3, d4, d5

x4cmul16s_h d6, d7, d8

x4cmul16s_l d9, d10, d11

#===----------------------------------------------------------------------===
# X2 Fractional Multiply Accumulate/Subtract (FmtALU64 — 3-operand)
# These accumulate into rd with the product of rs1 and rs2.
#===----------------------------------------------------------------------===

x2fmula32rs d0, d1, d2

x2fmula32rss d3, d4, d5

x2fmuls32rs d6, d7, d8

x2fmuls32rss d9, d10, d11

#===----------------------------------------------------------------------===
# Complex MAC accumulate (FmtALU64 — 3-operand)
#===----------------------------------------------------------------------===

x2fcmula32rs d0, d1, d2

x2fcmula32rss d3, d4, d5

#===----------------------------------------------------------------------===
# Ternary MAC forms (FmtMAC — 4-operand)
# These use a separate accumulator register (ra).
#===----------------------------------------------------------------------===

x4ff2mul16s d4, d5, d6, d7

x4ff2mula16s d0, d1, d2, d3

x4ff2muls16s d4, d5, d6, d7
