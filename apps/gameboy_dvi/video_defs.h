#ifndef VIDEO_DEFS_H
#define VIDEO_DEFS_H

#include "dvi.h"

// Game Boy capture geometry (2bpp packed)
#define DMG_PIXELS_X                160
#define DMG_PIXELS_Y                144
#define DMG_PIXEL_COUNT             (DMG_PIXELS_X * DMG_PIXELS_Y)

// Packed DMA buffers - 4 pixels per byte (2 bits each)
#define PACKED_FRAME_SIZE           (DMG_PIXEL_COUNT / 4)
#define PACKED_LINE_STRIDE_BYTES    (DMG_PIXELS_X / 4)

#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define HORIZONTAL_SCALE 4

#define VREG_VSEL VREG_VOLTAGE_1_10
#define DVI_TIMING dvi_timing_640x480p_60hz

#define SCANLINE_COUNT    (FRAME_HEIGHT / DVI_VERTICAL_REPEAT)
#define VERTICAL_OFFSET   (((SCANLINE_COUNT - DMG_PIXELS_Y) / 2) - 2)

#endif // VIDEO_DEFS_H
