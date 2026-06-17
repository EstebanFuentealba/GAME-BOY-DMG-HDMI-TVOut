#include "print_bridge.h"

#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/time.h"

#define PRINT_QUEUE_DEPTH 2
#define PRINT_PACKET_MAGIC 0x52504247u /* GBPR, little endian on UART */
#define PRINT_PACKET_VERSION 1
#define PRINT_DATA_CHUNK_SIZE 64
#define PRINT_FRAME_FLAG_DITHER 0x01
#define PRINT_PREPARE_ROWS_PER_TASK 8
#define PRINT_STATUS_POLL_INTERVAL_US 50000
#define PRINT_JOB_TIMEOUT_US 15000000

typedef enum {
    PRINT_PKT_START = 1,
    PRINT_PKT_DATA = 2,
    PRINT_PKT_END = 3,
    PRINT_PKT_CANCEL = 4,
    PRINT_PKT_STATUS = 0x80,
} print_packet_type_t;

typedef enum {
    WORK_IDLE = 0,
    WORK_PREPARE,
    WORK_SEND_START,
    WORK_SEND_DATA,
    WORK_SEND_END,
    WORK_WAIT_REMOTE,
} print_work_state_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t seq;
    uint32_t job_id;
    uint16_t length;
    uint16_t crc16;
} print_packet_header_t;

static uint8_t print_queue[PRINT_QUEUE_DEPTH][PACKED_FRAME_SIZE];
static uint8_t active_packed_frame[PACKED_FRAME_SIZE];
static uint8_t print_raster[PRINT_RASTER_SIZE];
static volatile uint8_t queue_head;
static volatile uint8_t queue_tail;
static volatile uint8_t queue_count;

static print_work_state_t work_state = WORK_IDLE;
static print_status_t status = PRINT_STATUS_IDLE;
static uint32_t completed_jobs;
static uint32_t dropped_jobs;
static uint32_t last_error_code;
static uint32_t active_job_id;
static uint16_t packet_seq;
static uint32_t raster_offset;
static uint64_t job_started_at;
static uint64_t last_status_poll_at;
static int prepare_y;
static int16_t dither_err_curr[PRINT_WIDTH_DOTS + 2];
static int16_t dither_err_next[PRINT_WIDTH_DOTS + 2];

static uint16_t crc16_ccitt_update(uint16_t crc, const uint8_t *data, size_t len)
{
    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static void send_packet(print_packet_type_t type, const uint8_t *payload, uint16_t payload_len)
{
    print_packet_header_t hdr = {
        .magic = PRINT_PACKET_MAGIC,
        .version = PRINT_PACKET_VERSION,
        .type = (uint8_t)type,
        .seq = packet_seq++,
        .job_id = active_job_id,
        .length = payload_len,
        .crc16 = 0xffff,
    };

    hdr.crc16 = crc16_ccitt_update(hdr.crc16, (const uint8_t *)&hdr.version, sizeof(hdr) - sizeof(hdr.magic) - sizeof(hdr.crc16));
    hdr.crc16 = crc16_ccitt_update(hdr.crc16, payload, payload_len);

    uart_write_blocking(PRINT_UART_ID, (const uint8_t *)&hdr, sizeof(hdr));
    if (payload_len)
        uart_write_blocking(PRINT_UART_ID, payload, payload_len);
}

static int pixel_luma(const uint8_t *packed_frame, int x, int y)
{
    const uint8_t *row = packed_frame + y * PACKED_LINE_STRIDE_BYTES;
    const uint8_t shade = (row[x >> 2] >> (6 - 2 * (x & 3))) & 3;
    return 255 - (int)shade * 85;
}

static void prepare_print_raster_begin(void)
{
    memset(print_raster, 0, sizeof(print_raster));
    memset(dither_err_curr, 0, sizeof(dither_err_curr));
    memset(dither_err_next, 0, sizeof(dither_err_next));
    prepare_y = 0;
}

static bool prepare_print_raster_step(const uint8_t *packed_frame)
{
    const int y_end = (prepare_y + PRINT_PREPARE_ROWS_PER_TASK > PRINT_HEIGHT_DOTS)
        ? PRINT_HEIGHT_DOTS
        : prepare_y + PRINT_PREPARE_ROWS_PER_TASK;

    for (int y = prepare_y; y < y_end; ++y) {
        const int src_y = (y * DMG_PIXELS_Y) / PRINT_HEIGHT_DOTS;
        uint8_t *dst_row = print_raster + y * PRINT_BYTES_PER_ROW;

        for (int x = 0; x < PRINT_WIDTH_DOTS; ++x) {
            const int src_x = (x * DMG_PIXELS_X) / PRINT_WIDTH_DOTS;
            int old_pixel = pixel_luma(packed_frame, src_x, src_y) + dither_err_curr[x + 1] / 16;
            if (old_pixel < 0)
                old_pixel = 0;
            else if (old_pixel > 255)
                old_pixel = 255;

            const bool black = old_pixel < 128;
            const int new_pixel = black ? 0 : 255;
            const int quant_error = old_pixel - new_pixel;

            if (black)
                dst_row[x >> 3] |= (uint8_t)(0x80u >> (x & 7));

            dither_err_curr[x + 2] += (int16_t)(quant_error * 7);
            dither_err_next[x] += (int16_t)(quant_error * 3);
            dither_err_next[x + 1] += (int16_t)(quant_error * 5);
            dither_err_next[x + 2] += (int16_t)quant_error;
        }

        memcpy(dither_err_curr, dither_err_next, sizeof(dither_err_curr));
        memset(dither_err_next, 0, sizeof(dither_err_next));
    }

    prepare_y = y_end;
    return prepare_y >= PRINT_HEIGHT_DOTS;
}

static bool pop_next_job(uint8_t *dst)
{
    uint32_t save = save_and_disable_interrupts();
    if (!queue_count) {
        restore_interrupts(save);
        return false;
    }
    const uint8_t tail = queue_tail;
    queue_tail = (uint8_t)((queue_tail + 1) % PRINT_QUEUE_DEPTH);
    --queue_count;
    restore_interrupts(save);

    memcpy(dst, print_queue[tail], PACKED_FRAME_SIZE);
    return true;
}

static void set_status(print_status_t next)
{
    if (status == next)
        return;

    status = next;
    printf("Print: %s\n", print_bridge_status_name(next));
}

void print_bridge_init(void)
{
    uart_init(PRINT_UART_ID, PRINT_UART_BAUD);
    gpio_set_function(PRINT_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(PRINT_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(PRINT_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(PRINT_UART_ID, true);

    gpio_init(PRINT_BUTTON_PIN);
    gpio_set_dir(PRINT_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(PRINT_BUTTON_PIN);

    set_status(PRINT_STATUS_IDLE);
}

bool print_bridge_enqueue_frame(const uint8_t *packed_frame)
{
    uint32_t save = save_and_disable_interrupts();

    if (queue_count >= PRINT_QUEUE_DEPTH) {
        ++dropped_jobs;
        restore_interrupts(save);
        set_status(PRINT_STATUS_QUEUE_FULL);
        return false;
    }

    const uint8_t head = queue_head;
    queue_head = (uint8_t)((queue_head + 1) % PRINT_QUEUE_DEPTH);
    ++queue_count;
    restore_interrupts(save);

    memcpy(print_queue[head], packed_frame, PACKED_FRAME_SIZE);
    set_status(PRINT_STATUS_QUEUED);
    return true;
}

void print_bridge_task(void)
{
    const uint64_t now = time_us_64();

    if (work_state != WORK_IDLE && now - job_started_at > PRINT_JOB_TIMEOUT_US) {
        last_error_code = 1;
        work_state = WORK_IDLE;
        set_status(PRINT_STATUS_CONNECTION_ERROR);
    }

    switch (work_state) {
    case WORK_IDLE:
        if (queue_count) {
            if (!pop_next_job(active_packed_frame))
                break;
            active_job_id++;
            packet_seq = 0;
            raster_offset = 0;
            job_started_at = now;
            prepare_print_raster_begin();
            work_state = WORK_PREPARE;
            set_status(PRINT_STATUS_PREPARING);
        } else if (status == PRINT_STATUS_DONE || status == PRINT_STATUS_QUEUE_FULL) {
            set_status(PRINT_STATUS_IDLE);
        }
        break;

    case WORK_PREPARE:
        if (prepare_print_raster_step(active_packed_frame)) {
            work_state = WORK_SEND_START;
            set_status(PRINT_STATUS_CONNECTING);
        }
        break;

    case WORK_SEND_START: {
        uint8_t payload[12];
        payload[0] = (uint8_t)(PRINT_WIDTH_DOTS);
        payload[1] = (uint8_t)(PRINT_WIDTH_DOTS >> 8);
        payload[2] = (uint8_t)(PRINT_HEIGHT_DOTS);
        payload[3] = (uint8_t)(PRINT_HEIGHT_DOTS >> 8);
        payload[4] = (uint8_t)(PRINT_BYTES_PER_ROW);
        payload[5] = (uint8_t)(PRINT_BYTES_PER_ROW >> 8);
        payload[6] = PRINT_FRAME_FLAG_DITHER;
        payload[7] = 0;
        payload[8] = (uint8_t)(PRINT_RASTER_SIZE);
        payload[9] = (uint8_t)(PRINT_RASTER_SIZE >> 8);
        payload[10] = (uint8_t)(PRINT_RASTER_SIZE >> 16);
        payload[11] = (uint8_t)(PRINT_RASTER_SIZE >> 24);
        send_packet(PRINT_PKT_START, payload, sizeof(payload));
        work_state = WORK_SEND_DATA;
        set_status(PRINT_STATUS_SENDING);
        break;
    }

    case WORK_SEND_DATA:
        if (raster_offset < PRINT_RASTER_SIZE) {
            const uint32_t remaining = PRINT_RASTER_SIZE - raster_offset;
            const uint16_t chunk = (remaining > PRINT_DATA_CHUNK_SIZE) ? PRINT_DATA_CHUNK_SIZE : (uint16_t)remaining;
            send_packet(PRINT_PKT_DATA, print_raster + raster_offset, chunk);
            raster_offset += chunk;
        }
        if (raster_offset >= PRINT_RASTER_SIZE)
            work_state = WORK_SEND_END;
        break;

    case WORK_SEND_END:
        send_packet(PRINT_PKT_END, NULL, 0);
        last_status_poll_at = 0;
        work_state = WORK_WAIT_REMOTE;
        set_status(PRINT_STATUS_PRINTING);
        break;

    case WORK_WAIT_REMOTE:
        if (now - last_status_poll_at >= PRINT_STATUS_POLL_INTERVAL_US) {
            last_status_poll_at = now;
            while (uart_is_readable(PRINT_UART_ID)) {
                const uint8_t remote = uart_getc(PRINT_UART_ID);
                if (remote == PRINT_STATUS_DONE) {
                    ++completed_jobs;
                    work_state = WORK_IDLE;
                    set_status(PRINT_STATUS_DONE);
                    break;
                }
                if (remote == PRINT_STATUS_CONNECTION_ERROR || remote == PRINT_STATUS_TRANSPORT_ERROR) {
                    last_error_code = remote;
                    work_state = WORK_IDLE;
                    set_status((print_status_t)remote);
                    break;
                }
            }
        }
        break;
    }
}

print_bridge_snapshot_t print_bridge_get_snapshot(void)
{
    return (print_bridge_snapshot_t) {
        .status = status,
        .queued_jobs = queue_count,
        .completed_jobs = completed_jobs,
        .dropped_jobs = dropped_jobs,
        .last_error_code = last_error_code,
    };
}

const char *print_bridge_status_name(print_status_t s)
{
    switch (s) {
    case PRINT_STATUS_IDLE: return "idle";
    case PRINT_STATUS_QUEUED: return "queued";
    case PRINT_STATUS_PREPARING: return "preparing image";
    case PRINT_STATUS_CONNECTING: return "connecting printer";
    case PRINT_STATUS_SENDING: return "sending data";
    case PRINT_STATUS_PRINTING: return "printing";
    case PRINT_STATUS_DONE: return "print completed";
    case PRINT_STATUS_QUEUE_FULL: return "print queue full";
    case PRINT_STATUS_CONNECTION_ERROR: return "connection error";
    case PRINT_STATUS_TRANSPORT_ERROR: return "transport error";
    default: return "unknown";
    }
}
