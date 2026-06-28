#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"

#include "video_capture.pio.h"
#include "video_defs.h"
#include "colors.h"
#include "splash.h"
#include "print_bridge.h"

#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

// Mode 0 — Fullscreen: fit-by-height, pillarbox left/right
#define FS_V_SAFE_TOP    12
#define FS_V_SAFE_BOTTOM 0
#define FS_ACTIVE_HEIGHT (FRAME_HEIGHT - FS_V_SAFE_TOP - FS_V_SAFE_BOTTOM)
#define FS_SCALED_WIDTH  ((DMG_PIXELS_X * FS_ACTIVE_HEIGHT) / DMG_PIXELS_Y)
#define FS_TOP_BORDER    FS_V_SAFE_TOP
#define FS_LEFT_BORDER   ((FRAME_WIDTH - FS_SCALED_WIDTH) / 2)

// Mode 2 — Small: smaller scale, extra vertical margins
#define SM_V_TOP         24
#define SM_V_BOTTOM       16
#define SM_ACTIVE_HEIGHT (FRAME_HEIGHT - SM_V_TOP - SM_V_BOTTOM)
#define SM_SCALED_WIDTH  ((DMG_PIXELS_X * SM_ACTIVE_HEIGHT) / DMG_PIXELS_Y)
#define SM_TOP_BORDER    SM_V_TOP
#define SM_LEFT_BORDER   ((FRAME_WIDTH - SM_SCALED_WIDTH) / 2)

// Frames without VSYNC before declaring no-signal (~2 sec at 60fps)
#define NO_SIGNAL_TIMEOUT 120

typedef enum { DISPLAY_FULLSCREEN = 0, DISPLAY_ORIGINAL, DISPLAY_SMALL, DISPLAY_COUNT } display_mode_t;

#define ASPECT_BTN_PIN  5
#define PALETTE_BTN_PIN 6

// GAMEBOY VIDEO INPUT (From level shifter)
#define HSYNC_PIN                   0
#define DATA_1_PIN                  1
#define DATA_0_PIN                  2
#define PIXEL_CLOCK_PIN             3
#define VSYNC_PIN                   4


static uint8_t packed_buffer_0[PACKED_FRAME_SIZE] = {0};
static uint8_t packed_buffer_1[PACKED_FRAME_SIZE] = {0};
static volatile uint8_t* packed_display_ptr = packed_buffer_0;

static PIO pio_video = pio1;
static uint video_sm = 0;
static uint video_offset = 0;

struct dvi_inst dvi0;

uint16_t line_buffers[8][FRAME_WIDTH];

void core1_main() {
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    while (queue_is_empty(&dvi0.q_colour_valid))
        __wfe();
    dvi_start(&dvi0);
    dvi_scanbuf_main_16bpp(&dvi0);
}

static void __no_inline_not_in_flash_func(gpio_callback)(uint gpio, uint32_t events)
{
    if (gpio == VSYNC_PIN) {
        video_capture_handle_vsync_irq(events);
    }
}

int main() {
    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
    
    setup_default_uart();

    // Initialize Game Boy input pins
    gpio_init(HSYNC_PIN);
    gpio_set_dir(HSYNC_PIN, GPIO_IN);
    gpio_init(DATA_1_PIN);
    gpio_set_dir(DATA_1_PIN, GPIO_IN);
    gpio_init(DATA_0_PIN);
    gpio_set_dir(DATA_0_PIN, GPIO_IN);
    gpio_init(PIXEL_CLOCK_PIN);
    gpio_set_dir(PIXEL_CLOCK_PIN, GPIO_IN);

    gpio_init(ASPECT_BTN_PIN);
    gpio_set_dir(ASPECT_BTN_PIN, GPIO_IN);
    gpio_pull_up(ASPECT_BTN_PIN);

    gpio_init(PALETTE_BTN_PIN);
    gpio_set_dir(PALETTE_BTN_PIN, GPIO_IN);
    gpio_pull_up(PALETTE_BTN_PIN);

    print_bridge_init();

    // Initial test pattern for the first frame
    for (int i = 0; i < PACKED_FRAME_SIZE; i++) {
        packed_buffer_0[i] = 0b11100100;
    }

    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    printf("Initializing PIO video capture...\n");
    video_offset = pio_add_program(pio_video, &video_capture_irq_program);
    video_capture_program_init(pio_video, video_sm, video_offset);

    int video_dma_chan = video_capture_dma_init(pio_video, video_sm, -1, packed_buffer_0, PACKED_FRAME_SIZE);
    if (video_dma_chan < 0) {
        printf("ERROR: Video capture DMA initialization failed!\n");
        while (1) { tight_loop_contents(); }
    }

    multicore_launch_core1(core1_main);

    irq_set_priority(IO_IRQ_BANK0, 0x00);
    gpio_set_irq_enabled_with_callback(VSYNC_PIN, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);

    uint8_t* packed_capture = packed_buffer_0;
    uint buf_idx = 0;
    display_mode_t display_mode = DISPLAY_FULLSCREEN;
    bool last_btn_state = true;
    int debounce_counter = 0;

    int palette_idx = 0;
    bool last_pal_btn_state = true;
    int pal_debounce_counter = 0;
    bool last_print_btn_state = true;
    int print_debounce_counter = 0;

    uint32_t last_frame_count = 0;
    int no_signal_counter = NO_SIGNAL_TIMEOUT;

    // Start first capture immediately
    video_capture_start_frame(pio_video, video_sm, packed_capture, PACKED_FRAME_SIZE);

    while (true) {

        // --- Input: polled once per frame ---
        bool btn_state = gpio_get(ASPECT_BTN_PIN);
        if (!btn_state && last_btn_state && debounce_counter <= 0) {
            display_mode = (display_mode_t)((display_mode + 1) % DISPLAY_COUNT);
            debounce_counter = 12;
        }
        last_btn_state = btn_state;
        if (debounce_counter > 0) debounce_counter--;

        bool pal_btn_state = gpio_get(PALETTE_BTN_PIN);
        if (!pal_btn_state && last_pal_btn_state && pal_debounce_counter <= 0) {
            palette_idx = (palette_idx + 1) % NUM_PALETTES;
            pal_debounce_counter = 12;
        }
        last_pal_btn_state = pal_btn_state;
        if (pal_debounce_counter > 0) pal_debounce_counter--;

        bool print_btn_state = gpio_get(PRINT_BUTTON_PIN);
        if (!print_btn_state && last_print_btn_state && print_debounce_counter <= 0) {
            if (no_signal_counter < NO_SIGNAL_TIMEOUT && !print_bridge_is_busy())
                print_bridge_enqueue_frame((const uint8_t *)packed_display_ptr);
            print_debounce_counter = 20;
        }
        last_print_btn_state = print_btn_state;
        if (print_debounce_counter > 0) print_debounce_counter--;

        // --- Signal detection ---
        uint32_t fc = video_capture_get_frame_count();
        if (fc != last_frame_count) {
            last_frame_count = fc;
            no_signal_counter = 0;
        } else if (no_signal_counter < NO_SIGNAL_TIMEOUT) {
            no_signal_counter++;
        }

        // --- Capture: swap at frame boundary only ---
        (void)video_capture_poll_complete();
        if (video_capture_frame_ready()) {
            uint8_t *completed = video_capture_get_frame();
            packed_capture = (completed == packed_buffer_0) ? packed_buffer_1 : packed_buffer_0;
            video_capture_start_frame(pio_video, video_sm, packed_capture, PACKED_FRAME_SIZE);
            __dmb();
            packed_display_ptr = (volatile uint8_t *)completed;
            __dmb();
        }

        print_bridge_task();

        // Single volatile read — use splash when no GB signal
        const uint8_t *const frame = (no_signal_counter >= NO_SIGNAL_TIMEOUT)
            ? splash_data
            : (const uint8_t *)packed_display_ptr;
        const uint16_t *const pal = palettes[palette_idx];
        const uint16_t bg = pal[3];

#define PUSH_SCANLINE(sl) do {                                          \
            const uint16_t *_p = (sl);                                  \
            queue_add_blocking_u32(&dvi0.q_colour_valid, &_p);          \
            while (queue_try_remove_u32(&dvi0.q_colour_free, &_p));     \
            buf_idx = (buf_idx + 1) & 7;                                \
        } while (0)

#define RENDER_SCALED(TOP, ACTIVE, LEFT, SCALED_W) do {                             \
            for (uint _y = 0; _y < (TOP); ++_y) {                                   \
                uint16_t *sl = line_buffers[buf_idx];                                \
                for (uint x = 0; x < FRAME_WIDTH; ++x) sl[x] = bg;                 \
                PUSH_SCANLINE(sl);                                                   \
            }                                                                        \
            for (uint _y = 0; _y < (ACTIVE); ++_y) {                                \
                const uint8_t *row = frame + (_y * DMG_PIXELS_Y / (ACTIVE)) * (DMG_PIXELS_X / 4); \
                uint16_t *sl = line_buffers[buf_idx];                                \
                for (uint x = 0; x < (LEFT); ++x) sl[x] = bg;                      \
                uint gb_x = 0, err = 0;                                             \
                for (uint x = (LEFT); x < (LEFT) + (SCALED_W); ++x) {              \
                    sl[x] = pal[(row[gb_x >> 2] >> (6 - 2 * (gb_x & 3))) & 3];    \
                    if ((err += DMG_PIXELS_X) >= (SCALED_W)) {                      \
                        err -= (SCALED_W); gb_x++;                                  \
                    }                                                                \
                }                                                                    \
                for (uint x = (LEFT) + (SCALED_W); x < FRAME_WIDTH; ++x) sl[x] = bg; \
                PUSH_SCANLINE(sl);                                                   \
            }                                                                        \
            for (uint _y = (TOP) + (ACTIVE); _y < FRAME_HEIGHT; ++_y) {             \
                uint16_t *sl = line_buffers[buf_idx];                                \
                for (uint x = 0; x < FRAME_WIDTH; ++x) sl[x] = bg;                 \
                PUSH_SCANLINE(sl);                                                   \
            }                                                                        \
        } while (0)

        switch (display_mode) {

            case DISPLAY_FULLSCREEN:
                RENDER_SCALED(FS_TOP_BORDER, FS_ACTIVE_HEIGHT, FS_LEFT_BORDER, FS_SCALED_WIDTH);
                break;

            case DISPLAY_ORIGINAL: {
                for (uint y = 0; y < 48; ++y) {
                    uint16_t *sl = line_buffers[buf_idx];
                    for (uint x = 0; x < FRAME_WIDTH; ++x) sl[x] = bg;
                    PUSH_SCANLINE(sl);
                }
                const uint8_t *row = frame;
                for (uint y = 0; y < DMG_PIXELS_Y; ++y, row += DMG_PIXELS_X / 4) {
                    uint16_t *sl = line_buffers[buf_idx];
                    for (uint dmg_x = 0; dmg_x < DMG_PIXELS_X / 4; ++dmg_x) {
                        const uint8_t b = row[dmg_x];
                        const uint dst = dmg_x << 3;
                        sl[dst+0] = sl[dst+1] = pal[(b >> 6) & 3];
                        sl[dst+2] = sl[dst+3] = pal[(b >> 4) & 3];
                        sl[dst+4] = sl[dst+5] = pal[(b >> 2) & 3];
                        sl[dst+6] = sl[dst+7] = pal[b & 3];
                    }
                    PUSH_SCANLINE(sl);
                }
                for (uint y = 0; y < 48; ++y) {
                    uint16_t *sl = line_buffers[buf_idx];
                    for (uint x = 0; x < FRAME_WIDTH; ++x) sl[x] = bg;
                    PUSH_SCANLINE(sl);
                }
                break;
            }

            case DISPLAY_SMALL:
                RENDER_SCALED(SM_TOP_BORDER, SM_ACTIVE_HEIGHT, SM_LEFT_BORDER, SM_SCALED_WIDTH);
                break;

            default: break;
        }

#undef RENDER_SCALED
#undef PUSH_SCANLINE
    }
}
