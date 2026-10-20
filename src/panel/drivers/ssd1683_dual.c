/*
 * Elecrow CrowPanel 5.79" -- 792x272 mono, two cascaded SSD1683s. See
 * ssd1683_dual.h for the family.
 *
 * RAM layout, from Elecrow's EPD_Init.cpp (EPD_Display) and Paint_SetPixel:
 * each controller is a 400x300 part with 396 sources wired, so the pair is
 * addressed as one 800-column buffer whose columns 396-403 are the seam and
 * never reach the glass. Columns 0-399 are the master's RAM, written X
 * increasing; columns 400-799 are the slave's, written X DECREASING from byte
 * 49, which is how the slave's mirrored source order lines back up. Both chips
 * take buffer row r at gate address 271 - r. The frame carries only the 792
 * visible columns and the seam is inserted here.
 *
 * Elecrow streams Y-first (entry mode AM = 1); this streams X-first with the
 * same X and Y directions, which fills both RAMs with identical bytes and lets
 * a buffer row go out as one write.
 *
 * Opcodes reach both chips; the slave's own copies (bit 7 set) reach only the
 * slave. So the master's window and plane go first and the slave's follow,
 * overwriting what the broadcast put in the slave's RAM.
 */
#include "app_config.h"          /* board.h -> PANEL_DRIVER_* selection */

#if defined(PANEL_DRIVER_SSD1683_DUAL)

#include "drivers/ssd1683_dual.h"
#include "../busy_sleep.h"
#include "diag_run.h"           /* BUSY timeouts latch a report for the next status */

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd_ssd1683";

/* SSD1683 opcodes; OR in SLAVE for the slave's copy. */
#define SSD_SLAVE       0x80
#define SSD_SWRESET     0x12
#define SSD_DATA_ENTRY  0x11
#define SSD_RAM_X       0x44
#define SSD_RAM_Y       0x45
#define SSD_RAM_X_ADDR  0x4e
#define SSD_RAM_Y_ADDR  0x4f
#define SSD_TEMP_SENSOR 0x18
#define SSD_BW_RAM      0x24
#define SSD_UPD_CTRL1   0x21
#define SSD_UPD_CTRL2   0x22
#define SSD_TRIGGER     0x20
#define SSD_SLEEP       0x10

/* Full refresh: load temperature, load LUT, display, power down. */
#define SSD_UPD_FULL    0xf7

/* Update control 1: red RAM bypassed as 0 (so the second plane never needs
 * writing), BW RAM normal, and B[4] = cascade application, which the
 * datasheet requires when the two chips share the master's clock on CL.
 * Elecrow's examples leave this at POR; GxEPD2 sets it. */
static const uint8_t UPD_CTRL1_FULL[] = {0x40, 0x10};

/* Controller geometry. */
#define CHIP_BYTES      50                       /* 400 sources / 8 */
#define BUF_BYTES_ROW   (2 * CHIP_BYTES)         /* 800-column buffer row */
#define SEAM_X          396                      /* first seam column */
#define SEAM_W          8
#define GATES           EPD_HEIGHT               /* 272 */

#ifndef EPD_ROTATE_180
#define EPD_ROTATE_180 0
#endif

_Static_assert(EPD_WIDTH == 2 * SEAM_X, "frame must be the two 396-column halves");

static spi_device_handle_t s_spi;
static bool s_port_inited = false;

/* ---------- low-level SPI/GPIO ---------- */

static esp_err_t spi_tx_raw(const uint8_t *data, size_t len)
{
    const size_t CHUNK = 4096;
    spi_transaction_t t;
    for (size_t off = 0; off < len; off += CHUNK) {
        size_t n = (len - off > CHUNK) ? CHUNK : (len - off);
        memset(&t, 0, sizeof t);
        t.length = n * 8;
        t.tx_buffer = data + off;
        esp_err_t err = spi_device_polling_transmit(s_spi, &t);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spi tx fail at off=%u: %s", (unsigned)off,
                     esp_err_to_name(err));
            return err;
        }
    }
    return ESP_OK;
}

static void send_cmd(uint8_t cmd)
{
    gpio_set_level(EPD_PIN_DC, 0);
    spi_tx_raw(&cmd, 1);
}

static void send_data(const uint8_t *buf, size_t len)
{
    gpio_set_level(EPD_PIN_DC, 1);
    spi_tx_raw(buf, len);
}

static void cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    gpio_set_level(EPD_PIN_CS, 0);
    send_cmd(cmd);
    if (len) send_data(data, len);
    gpio_set_level(EPD_PIN_CS, 1);
}

/* BUSY HIGH = busy, as on the SSD1677. Bounded so a stuck panel still reaches
 * its power-down. */
#define SSD_BUSY_TIMEOUT_MS 30000

static bool wait_idle(void)
{
    int waited = 0;
    bool warned = false;
    while (gpio_get_level(EPD_PIN_BUSY)) {
        waited += epd_busy_sleep(EPD_PIN_BUSY, 0, 10);
        if (!warned && waited >= 10000) {
            ESP_LOGW(TAG, "BUSY still high after 10 s; panel may be stuck");
            warned = true;
        }
        if (waited >= SSD_BUSY_TIMEOUT_MS) {
            ESP_LOGE(TAG, "BUSY never cleared in %d ms; continuing so the panel "
                          "still gets powered down", SSD_BUSY_TIMEOUT_MS);
            diag_note_paint(DIAG_PAINT_READY_TIMEOUT);
            return false;
        }
    }
    return true;
}

static void hw_reset(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(10));
}

/* Hold the bus for a whole frame: CS is driven by hand across many chunked
 * transactions (see ssd1677_gray.c). The SD card is on its own bus here, so
 * this is belt and braces rather than a fix. */
static void bus_hold(void)    { spi_device_acquire_bus(s_spi, portMAX_DELAY); }
static void bus_release(void) { spi_device_release_bus(s_spi); }

/* ---------- frame -> controller buffer ---------- */

static inline int frame_bit(const uint8_t *image, int fx, int fy)
{
    return (image[(size_t)fy * (EPD_WIDTH / 8) + (fx >> 3)] >> (7 - (fx & 7))) & 1;
}

/* Build buffer row r (BUF_BYTES_ROW bytes, seam white) from the frame, or a
 * solid fill when image is NULL. Unrotated, buffer column bx shows frame
 * column bx left of the seam and bx - 8 right of it, frame row r. Rotated,
 * the frame is turned 180 degrees first, which is Elecrow's Rotation 180. */
static void build_row(const uint8_t *image, uint8_t fill, int r, uint8_t *out)
{
    if (!image) {
        memset(out, fill, BUF_BYTES_ROW);
        return;
    }
    memset(out, 0xff, BUF_BYTES_ROW);
    const int fy = EPD_ROTATE_180 ? (GATES - 1 - r) : r;
    for (int bx = 0; bx < BUF_BYTES_ROW * 8; bx++) {
        if (bx >= SEAM_X && bx < SEAM_X + SEAM_W) continue;
        int x = (bx < SEAM_X) ? bx : bx - SEAM_W;
        int fx = EPD_ROTATE_180 ? (EPD_WIDTH - 1 - x) : x;
        if (!frame_bit(image, fx, fy))
            out[bx >> 3] &= (uint8_t)~(1u << (7 - (bx & 7)));
    }
}

/* Window and address counter for one chip, entry mode X-first, Y decreasing.
 * Master: X 0 -> 49 increasing. Slave: X 49 -> 0 decreasing. */
static void set_window(bool slave)
{
    const uint8_t s = slave ? SSD_SLAVE : 0;
    const uint8_t entry[] = {slave ? 0x00 : 0x01};   /* X dec|inc, Y dec, AM=0 */
    const uint8_t xwin[]  = {slave ? CHIP_BYTES - 1 : 0, slave ? 0 : CHIP_BYTES - 1};
    const uint8_t ywin[]  = {(GATES - 1) & 0xff, (GATES - 1) >> 8, 0x00, 0x00};
    const uint8_t xaddr[] = {slave ? CHIP_BYTES - 1 : 0};
    const uint8_t yaddr[] = {(GATES - 1) & 0xff, (GATES - 1) >> 8};

    cmd_data(SSD_DATA_ENTRY | s, entry, sizeof entry);
    cmd_data(SSD_RAM_X | s, xwin, sizeof xwin);
    cmd_data(SSD_RAM_Y | s, ywin, sizeof ywin);
    cmd_data(SSD_RAM_X_ADDR | s, xaddr, sizeof xaddr);
    cmd_data(SSD_RAM_Y_ADDR | s, yaddr, sizeof yaddr);
}

/* Write one chip's BW plane: buffer row r, master half or slave half. */
static void send_plane(const uint8_t *image, uint8_t fill, bool slave)
{
    uint8_t row[BUF_BYTES_ROW];

    set_window(slave);
    gpio_set_level(EPD_PIN_CS, 0);
    send_cmd(SSD_BW_RAM | (slave ? SSD_SLAVE : 0));
    for (int r = 0; r < GATES; r++) {
        build_row(image, fill, r, row);
        send_data(row + (slave ? CHIP_BYTES : 0), CHIP_BYTES);
    }
    gpio_set_level(EPD_PIN_CS, 1);
}

static void paint(const uint8_t *image, uint8_t fill, const char *what)
{
    bus_hold();
    send_plane(image, fill, false);
    send_plane(image, fill, true);

    cmd_data(SSD_UPD_CTRL1, UPD_CTRL1_FULL, sizeof UPD_CTRL1_FULL);
    const uint8_t upd[] = {SSD_UPD_FULL};
    int64_t t0 = esp_timer_get_time();
    cmd_data(SSD_UPD_CTRL2, upd, sizeof upd);
    cmd_data(SSD_TRIGGER, NULL, 0);
    wait_idle();
    ESP_LOGI(TAG, "%s done (%lld ms)", what,
             (long long)((esp_timer_get_time() - t0) / 1000));
    bus_release();
}

/* ---------- driver entry points ---------- */

static esp_err_t ssd1683_port_init(void)
{
    if (s_port_inited) return ESP_OK;

    gpio_config_t out = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << EPD_PIN_RST) | (1ULL << EPD_PIN_DC) |
                        (1ULL << EPD_PIN_CS) | (1ULL << EPD_PIN_EN),
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out));

    gpio_config_t in = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << EPD_PIN_BUSY),
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    gpio_set_level(EPD_PIN_CS, 1);
    gpio_set_level(EPD_PIN_DC, 0);
    gpio_set_level(EPD_PIN_RST, 1);

    spi_bus_config_t bus = {
        .miso_io_num = -1,
        .mosi_io_num = EPD_PIN_MOSI,
        .sclk_io_num = EPD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    spi_device_interface_config_t dev = {
        .clock_speed_hz = EPD_SPI_HZ,
        .mode = 0,
        .spics_io_num = -1,            /* we drive CS by hand */
        .queue_size = 1,
    };
    esp_err_t bus_err = spi_bus_initialize(EPD_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (bus_err != ESP_OK && bus_err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(bus_err);
    }
    ESP_ERROR_CHECK(spi_bus_add_device(EPD_SPI_HOST, &dev, &s_spi));

    s_port_inited = true;
    return ESP_OK;
}

static void ssd1683_init(void)
{
    gpio_hold_dis(EPD_PIN_EN);
    gpio_set_level(EPD_PIN_EN, 1);   /* panel power */
    vTaskDelay(pdMS_TO_TICKS(10));
    hw_reset();
    wait_idle();

    cmd_data(SSD_SWRESET, NULL, 0);  /* reaches both chips */
    wait_idle();

    static const uint8_t TSENS[] = {0x80};       /* internal temperature sensor */
    cmd_data(SSD_TEMP_SENSOR, TSENS, sizeof TSENS);

    ESP_LOGI(TAG, "init complete (2x SSD1683, frame %dx%d%s)",
             EPD_WIDTH, EPD_HEIGHT, EPD_ROTATE_180 ? ", rotated 180" : "");
}

static void ssd1683_display(const uint8_t *image)
{
    paint(image, 0, "refresh");
}

static void ssd1683_clear(uint8_t color)
{
    paint(NULL, (color & 1) ? 0xff : 0x00, "clear");
}

/* Bring-up: four alternating bands starting BLACK, a white wedge in the
 * frame's top-left, and a 1px stripe block in the bottom half (left:
 * vertical stripes, bit order; right: horizontal stripes, row stride).
 *
 * What to look for:
 *   wedge top-left                   -> EPD_ROTATE_180 is right
 *   wedge bottom-right               -> flip EPD_ROTATE_180
 *   wedge top-right or bottom-left   -> one axis is mirrored; the RAM
 *                                       directions in set_window are wrong
 *   a break or doubled column at mid-width -> the seam is off */
static void ssd1683_show_color_bars(void)
{
    uint8_t *frame = malloc(EPD_BUF_BYTES);
    if (!frame) return;
    const int BAND_H = EPD_HEIGHT / 4;
    const int stride = EPD_WIDTH / 8;

    for (int fy = 0; fy < EPD_HEIGHT; fy++) {
        int band = fy / BAND_H;
        if (band > 3) band = 3;
        memset(frame + (size_t)fy * stride, (band & 1) ? 0xff : 0x00, stride);
    }

    for (int fy = EPD_HEIGHT / 2; fy < EPD_HEIGHT; fy++) {
        for (int fx = 0; fx < EPD_WIDTH; fx++) {
            int black = (fx < EPD_WIDTH / 2) ? (fx & 1) : (fy & 1);
            size_t i = (size_t)fy * stride + (fx >> 3);
            uint8_t m = (uint8_t)(1u << (7 - (fx & 7)));
            if (black) frame[i] &= (uint8_t)~m;
            else       frame[i] |= m;
        }
    }

    const int WEDGE = 48;   /* px, a multiple of 8 */
    for (int fy = 0; fy < WEDGE; fy++)
        memset(frame + (size_t)fy * stride, 0xff, WEDGE / 8);

    ssd1683_display(frame);
    free(frame);
}

/* Transport check: alternating pixels across the whole frame. */
static void ssd1683_show_palette_sweep(void)
{
    uint8_t *frame = malloc(EPD_BUF_BYTES);
    if (!frame) return;
    memset(frame, 0xAA, EPD_BUF_BYTES);   /* 1px vertical stripes */
    ssd1683_display(frame);
    free(frame);
}

static void ssd1683_sleep(void)
{
    static const uint8_t DEEP[] = {0x01};
    cmd_data(SSD_SLEEP, DEEP, sizeof DEEP);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(EPD_PIN_EN, 0);   /* cut panel power */
}

/* ---------- exported vtable ---------- */

const epd_driver_t ssd1683_dual_driver = {
    .info = {
        .name      = "SSD1683 x2 5.79\" (792x272, mono 1bpp)",
        .width     = EPD_WIDTH,
        .height    = EPD_HEIGHT,
        .bpp       = 1,
        .buf_bytes = EPD_BUF_BYTES,
        .grayscale = false,
    },
    .port_init          = ssd1683_port_init,
    .init               = ssd1683_init,
    .clear              = ssd1683_clear,
    .display            = ssd1683_display,
    .show_color_bars    = ssd1683_show_color_bars,
    .show_palette_sweep = ssd1683_show_palette_sweep,
    .sleep              = ssd1683_sleep,
};

#endif /* PANEL_DRIVER_SSD1683_DUAL */
