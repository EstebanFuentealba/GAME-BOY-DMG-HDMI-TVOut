#pragma once
#include <stdint.h>

// Compile-time RGB888 → RGB565 conversion
#define _RGB(c) ((uint16_t)((((c) >> 19) & 0x1F) << 11 | (((c) >> 10) & 0x3F) << 5 | (((c) >> 3) & 0x1F)))

// 4-color palette (GB indices 0=lightest → 3=darkest)
typedef uint16_t palette_t[4];

static const palette_t palettes[] = {
    // 0: Default DMG green (original)
    { 0x8D65, 0x6CA4, 0x4345, 0x2206 },
    // 1: Black and White
    { _RGB(0xF7F3F7), _RGB(0xB5B2B5), _RGB(0x4E4C4E), _RGB(0x000000) },
    // 2: Inverted
    { _RGB(0x000000), _RGB(0x4E4C4E), _RGB(0xB5B2B5), _RGB(0xF7F3F7) },
    // 3: DMG
    { _RGB(0x7B8210), _RGB(0x5A7942), _RGB(0x39594A), _RGB(0x294139) },
    // 4: Game Boy Pocket
    { _RGB(0xC6CBA5), _RGB(0x8C926B), _RGB(0x4A5139), _RGB(0x181818) },
    // 5: Game Boy Light
    { _RGB(0x00B284), _RGB(0x8C926B), _RGB(0x00694A), _RGB(0x005139) },
    // // 6: SGB 1A
    // { _RGB(0xF7E3C6), _RGB(0xD6924A), _RGB(0xA52821), _RGB(0x311852) },
    // // 7: SGB 1B
    // { _RGB(0xD6D3BD), _RGB(0xC6AA73), _RGB(0xAD5110), _RGB(0x000000) },
    // // 8: SGB 1C
    // { _RGB(0xF7BAF7), _RGB(0xE79252), _RGB(0x943863), _RGB(0x393894) },
    // // 9: SGB 1D
    // { _RGB(0xF7F3A5), _RGB(0xBD824A), _RGB(0xF70000), _RGB(0x521800) },
    // // 10: SGB 1E
    // { _RGB(0xF7D3AD), _RGB(0x7BBA7B), _RGB(0x6B8A42), _RGB(0x5A3821) },
    // // 11: SGB 1F
    // { _RGB(0xD6E3F7), _RGB(0xDE8A52), _RGB(0xA50000), _RGB(0x004110) },
    // // 12: SGB 1G
    // { _RGB(0x000052), _RGB(0x009AE7), _RGB(0x7B7900), _RGB(0xF7F35A) },
    // // 13: SGB 1H
    // { _RGB(0xF7E3DE), _RGB(0xF7B28C), _RGB(0x844100), _RGB(0x311800) },
    // // 14: SGB 2A
    // { _RGB(0xEFC39C), _RGB(0xBD8A4A), _RGB(0x297900), _RGB(0x000000) },
    // // 15: SGB 2B
    // { _RGB(0xF7F3F7), _RGB(0xF7E352), _RGB(0xF73000), _RGB(0x52005A) },
    // // 16: SGB 2C
    // { _RGB(0xF7F3F7), _RGB(0xE78A8C), _RGB(0x7B30E7), _RGB(0x292894) },
    // // 17: SGB 2D
    // { _RGB(0xF7F39C), _RGB(0x00F300), _RGB(0xF73000), _RGB(0x000052) },
    // // 18: SGB 2E
    // { _RGB(0xF7C384), _RGB(0x94AADE), _RGB(0x291063), _RGB(0x100810) },
    // // 19: SGB 2F
    // { _RGB(0xCEF3F7), _RGB(0xF79252), _RGB(0x9C0000), _RGB(0x180000) },
    // // 20: SGB 2G
    // { _RGB(0x6BB239), _RGB(0xDE5142), _RGB(0xDEB284), _RGB(0x001800) },
    // // 21: SGB 2H
    // { _RGB(0xF7F3F7), _RGB(0xB5B2B5), _RGB(0x737173), _RGB(0x000000) },
    // // 22: SGB 3A
    // { _RGB(0xF7CB94), _RGB(0x73BABD), _RGB(0xF76129), _RGB(0x314963) },
    // // 23: SGB 3B
    // { _RGB(0xD6D3BD), _RGB(0xDE8221), _RGB(0x005100), _RGB(0x001010) },
    // // 24: SGB 3C
    // { _RGB(0xDEA2C6), _RGB(0xF7F37B), _RGB(0x00B2F7), _RGB(0x21205A) },
    // // 25: SGB 3D
    // { _RGB(0xEFF3B5), _RGB(0xDEA27B), _RGB(0x96AD52), _RGB(0x000000) },
    // // 26: SGB 3E
    // { _RGB(0xF7F3BD), _RGB(0xDEAA6B), _RGB(0xAD7921), _RGB(0x524973) },
    // // 27: SGB 3F
    // { _RGB(0x7B79C6), _RGB(0xF769F7), _RGB(0xF7CB00), _RGB(0x424142) },
    // // 28: SGB 3G
    // { _RGB(0x63D352), _RGB(0xF7F3F7), _RGB(0xC63039), _RGB(0x390000) },
    // // 29: SGB 3H
    // { _RGB(0xDEF39C), _RGB(0x7BC339), _RGB(0x4A8A18), _RGB(0x081800) },
    // // 30: SGB 4A
    // { _RGB(0xEFA26B), _RGB(0x7BA2F7), _RGB(0xCE00CE), _RGB(0x00007B) },
    // // 31: SGB 4B
    // { _RGB(0xEFE3EF), _RGB(0xE79A63), _RGB(0x427939), _RGB(0x180808) },
    // // 32: SGB 4C
    // { _RGB(0xF7DBDE), _RGB(0xF7F37B), _RGB(0x949ADE), _RGB(0x080000) },
    // // 33: SGB 4D
    // { _RGB(0xF7F39C), _RGB(0x94C3C6), _RGB(0x4A697B), _RGB(0x08204A) },
    // // 34: SGB 4E
    // { _RGB(0xF7D3A5), _RGB(0xDEA27B), _RGB(0x7B598C), _RGB(0x002031) },
    // // 35: SGB 4F
    // { _RGB(0xB5CBCE), _RGB(0xD682D6), _RGB(0x84009C), _RGB(0x390000) },
    // // 36: SGB 4G
    // { _RGB(0xADDB18), _RGB(0xB5205A), _RGB(0x291000), _RGB(0x008263) },
    // // 37: SGB 4H
    // { _RGB(0xF7F3C6), _RGB(0xB5BA5A), _RGB(0x848A42), _RGB(0x425129) },
};

#define NUM_PALETTES ((int)(sizeof(palettes) / sizeof(palettes[0])))
