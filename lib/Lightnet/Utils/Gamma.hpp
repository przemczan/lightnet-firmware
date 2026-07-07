#pragma once

// This file is shared between the controller (ESP32) and panel (AVR) builds.
// Controller: Arduino.h resolves PROGMEM/pgm_read_byte to ESP's own compatibility shim.
// Panel: avr/pgmspace.h directly — the panel build has no Arduino.h at all (hardware redesign
// plan §10) — see its note on this exact gotcha (swapping this blindly to avr/pgmspace.h once
// broke the controller build, since ESP32 has no such header).
#ifdef LIGHTNET_TARGET_CONTROLLER
    #include <Arduino.h>
#else
    #include <avr/pgmspace.h>
#endif
#include <stdint.h>

// Standard gamma 2.2: table[i] = round((i/255)^2.2 * 255)
// i=0 -> 0, i=255 -> 255, no dead zone, reaches full output.
//
// One shared table for all three channels — gammaValueR/G/B all read gammaTable; there is no
// per-channel gamma curve today.
const uint8_t PROGMEM gammaTable[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6,
    6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11, 11, 12,
    12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 16, 17, 17, 18, 19, 19,
    20, 21, 21, 22, 22, 23, 23, 24, 24, 25, 26, 26, 27, 28, 28, 29,
    30, 31, 31, 32, 33, 33, 34, 35, 36, 36, 37, 38, 39, 39, 40, 41,
    42, 43, 44, 44, 45, 46, 47, 48, 49, 50, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71,
    72, 73, 74, 75, 76, 78, 79, 80, 81, 82, 83, 85, 86, 87, 88, 90,
    91, 93, 94, 95, 97, 98, 99, 100, 102, 103, 104, 106, 107, 109, 110, 111,
    113, 114, 116, 117, 119, 120, 122, 123, 124, 126, 127, 129, 130, 132, 134, 135,
    137, 139, 140, 142, 143, 145, 147, 148, 150, 151, 153, 155, 156, 158, 160, 161,
    163, 165, 166, 168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190,
    192, 194, 196, 198, 200, 202, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
    223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248, 251, 253, 255 };

uint8_t gammaValueR(uint8_t value);
uint8_t gammaValueG(uint8_t value);
uint8_t gammaValueB(uint8_t value);
