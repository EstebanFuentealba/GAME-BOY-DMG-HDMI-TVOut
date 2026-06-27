#ifndef PRINT_BRIDGE_H
#define PRINT_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/uart.h"
#include "video_defs.h"

#define PRINT_UART_ID uart1
#define PRINT_UART_TX_PIN 8
#define PRINT_UART_RX_PIN 9
#define PRINT_UART_BAUD 921600

#define PRINT_BUTTON_PIN 7
#define PRINT_CONNECTED_LED_PIN 10
#define PRINT_RESET_SWITCH_PIN 11
#define PRINT_FRAME_SELECT_PIN 12

#define PRINT_WIDTH_DOTS 384
#define PRINT_SCALE_Y_NUM PRINT_WIDTH_DOTS
#define PRINT_SCALE_Y_DEN DMG_PIXELS_X
#define PRINT_HEADER_SOURCE_WIDTH 160
#define PRINT_HEADER_SOURCE_HEIGHT 58
#define PRINT_HEADER_HEIGHT_DOTS ((PRINT_HEADER_SOURCE_HEIGHT * PRINT_SCALE_Y_NUM + (PRINT_SCALE_Y_DEN / 2)) / PRINT_SCALE_Y_DEN)
#define PRINT_CAPTURE_HEIGHT_DOTS ((DMG_PIXELS_Y * PRINT_SCALE_Y_NUM + (PRINT_SCALE_Y_DEN / 2)) / PRINT_SCALE_Y_DEN)
#define PRINT_HEIGHT_DOTS (PRINT_HEADER_HEIGHT_DOTS + PRINT_CAPTURE_HEIGHT_DOTS)
#define PRINT_BYTES_PER_ROW (PRINT_WIDTH_DOTS / 8)
#define PRINT_RASTER_SIZE (PRINT_BYTES_PER_ROW * PRINT_HEIGHT_DOTS)

typedef enum {
    PRINT_STATUS_IDLE = 0,
    PRINT_STATUS_QUEUED,
    PRINT_STATUS_PREPARING,
    PRINT_STATUS_CONNECTING,
    PRINT_STATUS_SENDING,
    PRINT_STATUS_PRINTING,
    PRINT_STATUS_DONE,
    PRINT_STATUS_QUEUE_FULL,
    PRINT_STATUS_CONNECTION_ERROR,
    PRINT_STATUS_TRANSPORT_ERROR,
    PRINT_STATUS_PRINTER_CONNECTED,
    PRINT_STATUS_PRINTER_DISCONNECTED,
} print_status_t;

typedef struct {
    print_status_t status;
    uint32_t queued_jobs;
    uint32_t completed_jobs;
    uint32_t dropped_jobs;
    uint32_t last_error_code;
    bool printer_connected;
} print_bridge_snapshot_t;

void print_bridge_init(void);
bool print_bridge_enqueue_frame(const uint8_t *packed_frame);
bool print_bridge_is_busy(void);
void print_bridge_task(void);
print_bridge_snapshot_t print_bridge_get_snapshot(void);
const char *print_bridge_status_name(print_status_t status);

#endif
