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
 *
 * EPD_DUAL_DEPTH builds also take a 2bpp frame (display_gray) and drive both
 * RAM planes through an uploaded 4-gray waveform; see the gray section below.
 * A 1bpp frame paints exactly as it does on a mono-only build.
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
#define SSD_RED_RAM     0x26
#define SSD_DRIVER_OUT  0x01
#define SSD_GATE_V      0x03
#define SSD_SOURCE_V    0x04
#define SSD_BOOST       0x0c
#define SSD_VCOM        0x2c
#define SSD_LUT         0x32
#define SSD_BORDER      0x3c
#define SSD_END_OPT     0x3f
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

#ifdef EPD_DUAL_DEPTH
/* ---------- 4-level grayscale (display_gray) ----------
 *
 * The SSD1683 has no built-in 4-gray waveform on this glass, so the waveform
 * is uploaded to the LUT register and the refresh runs from it (update 0xCF:
 * no temperature or OTP LUT load). Both RAMs are live, so update control 1
 * drops the red-RAM bypass and keeps the cascade bit.
 *
 * Waveform, voltages and plane encoding are Good Display's 4-gray set for
 * their SSD1683 4.2" glass, as Adafruit_EPD ships it for that panel
 * (ThinkInk_420_Grayscale4_MFGN.h): 227 LUT bytes to 0x32, then the
 * end-option, gate, source and VCOM bytes that follow them in Good Display's
 * 233-byte tables. The CrowPanel's GDEY0579T93 is the same vendor and
 * controller but not the same glass; benched on it 2026-10-20, it holds four
 * clean levels with the waveform as published, the panel's own VCOM and the
 * normal end option (see SSD1683_GRAY_SET_VCOM and SSD1683_GRAY_EOPT).
 *
 * Plane bits for the frame's level g (0 = black .. 3 = white), from Adafruit's
 * layer table (white 00, light 01, dark 10, black 11 with both buffers
 * inverted, buffer 0 -> 0x24, buffer 1 -> 0x26):
 *
 *        white  light-gray  dark-gray  black         (g = 3    2    1    0)
 *   0x24:  1        0           1        0           (bit =  g       & 1)
 *   0x26:  1        1           0        0           (bit = (g >> 1) & 1)
 */
static const uint8_t UPD_CTRL1_GRAY[] = {0x00, 0x10};
#define SSD_UPD_GRAY    0xcf

static const uint8_t LUT_4G[227] = {
    0x01, 0x0A, 0x1B, 0x0F, 0x03, 0x01, 0x01, 0x05, 0x0A, 0x01, 0x0A, 0x01,
    0x01, 0x01, 0x05, 0x08, 0x03, 0x02, 0x04, 0x01, 0x01, 0x01, 0x04, 0x04,
    0x02, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x0A, 0x1B, 0x0F, 0x03, 0x01,
    0x01, 0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01, 0x05, 0x48, 0x03, 0x82,
    0x84, 0x01, 0x01, 0x01, 0x84, 0x84, 0x82, 0x00, 0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x0A, 0x1B, 0x8F, 0x03, 0x01, 0x01, 0x05, 0x4A, 0x01, 0x8A, 0x01,
    0x01, 0x01, 0x05, 0x48, 0x83, 0x82, 0x04, 0x01, 0x01, 0x01, 0x04, 0x04,
    0x02, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x8A, 0x1B, 0x8F, 0x03, 0x01,
    0x01, 0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01, 0x05, 0x48, 0x83, 0x02,
    0x04, 0x01, 0x01, 0x01, 0x04, 0x04, 0x02, 0x00, 0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x8A, 0x9B, 0x8F, 0x03, 0x01, 0x01, 0x05, 0x4A, 0x01, 0x8A, 0x01,
    0x01, 0x01, 0x05, 0x48, 0x03, 0x42, 0x04, 0x01, 0x01, 0x01, 0x04, 0x04,
    0x42, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
};
/* End option (0x3F). The published set sends 0x07, which keeps the sources at
 * their last level once the waveform ends; on this glass the image then went
 * soft a second or two after every refresh as that charge bled away (bench,
 * 2026-10-20). 0x22, the normal end with the sources back to ground, holds it. */
#ifndef SSD1683_GRAY_EOPT
#define SSD1683_GRAY_EOPT 0x22
#endif
static const uint8_t END_OPT_4G[] = {SSD1683_GRAY_EOPT};
static const uint8_t GATE_V_4G[]  = {0x17};
static const uint8_t SRC_V_4G[]   = {0x41, 0xA8, 0x32};
static const uint8_t VCOM_4G[]    = {0x30};

/* Which of the 4.2" set's rails to impose on this glass: 1 writes the value
 * above, 0 leaves the panel's own (OTP) value. Bench, 2026-10-20: the gate and
 * source rails are needed (without them the greys came out worse), VCOM is
 * not, so it stays the panel's.
 *
 * Also learned on that bench, so nobody retries it: the LUT is five per-level
 * waveforms (four pixel states, then VCOM) of six 7-byte phase groups, and
 * group 3, a short closing pass, is what clears the previous image. Dropping
 * or shortening it sharpened edges only at the price of ghosting, and a mono
 * pre-paint to clear instead cost a second flash; the softness was the end
 * option above all along. */
#ifndef SSD1683_GRAY_SET_VCOM
#define SSD1683_GRAY_SET_VCOM   0
#endif
#ifndef SSD1683_GRAY_SET_RAILS
#define SSD1683_GRAY_SET_RAILS  1   /* gate 0x03 + source 0x04 */
#endif
static const uint8_t BOOST_4G[]   = {0x8B, 0x9C, 0xA4, 0x0F};
static const uint8_t BORDER_4G[]  = {0x03};

#define PLANE_BIT(g, plane) ((plane) == 1 ? ((g) & 1) : (((g) >> 1) & 1))

/* A grey paint leaves the border, booster and rails at the 4-gray values;
 * the next mono paint in the same wake re-inits so it gets its own. */
static bool s_gray_dirty = false;
#endif /* EPD_DUAL_DEPTH */

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

/* The plane bit (1 = white side) a frame pixel contributes. plane 0 reads a
 * 1bpp frame, whose bit is the BW RAM bit directly; planes 1 (0x24) and 2
 * (0x26) read a 2bpp frame, whose level splits across the two RAMs. */
static inline int frame_bit(const uint8_t *image, int fx, int fy, int plane)
{
#ifdef EPD_DUAL_DEPTH
    if (plane) {
        const uint8_t b = image[(size_t)fy * (EPD_WIDTH / 4) + (fx >> 2)];
        const uint8_t g = (uint8_t)((b >> ((3 - (fx & 3)) * 2)) & 0x3);
        return PLANE_BIT(g, plane);
    }
#endif
    return (image[(size_t)fy * (EPD_WIDTH / 8) + (fx >> 3)] >> (7 - (fx & 7))) & 1;
}

/* Build buffer row r (BUF_BYTES_ROW bytes, seam white) of one plane from the
 * frame, or a solid fill when image is NULL. Unrotated, buffer column bx shows
 * frame column bx left of the seam and bx - 8 right of it, frame row r.
 * Rotated, the frame is turned 180 degrees first, which is Elecrow's Rotation
 * 180. */
static void build_row(const uint8_t *image, uint8_t fill, int r, int plane,
                      uint8_t *out)
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
        if (!frame_bit(image, fx, fy, plane))
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

/* Write one chip's half of a RAM plane, master or slave. plane 0 is a 1bpp
 * frame into BW RAM (0x24); planes 1 and 2 are a 2bpp frame's halves, into
 * 0x24 and the red RAM (0x26). */
static void send_plane(const uint8_t *image, uint8_t fill, bool slave, int plane)
{
    uint8_t row[BUF_BYTES_ROW];

    set_window(slave);
    gpio_set_level(EPD_PIN_CS, 0);
    send_cmd((plane == 2 ? SSD_RED_RAM : SSD_BW_RAM) | (slave ? SSD_SLAVE : 0));
    for (int r = 0; r < GATES; r++) {
        build_row(image, fill, r, plane, row);
        send_data(row + (slave ? CHIP_BYTES : 0), CHIP_BYTES);
    }
    gpio_set_level(EPD_PIN_CS, 1);
}

static void ssd1683_init(void);

static void paint(const uint8_t *image, uint8_t fill, const char *what)
{
#ifdef EPD_DUAL_DEPTH
    if (s_gray_dirty) { ssd1683_init(); s_gray_dirty = false; }
#endif
    bus_hold();
    send_plane(image, fill, false, 0);
    send_plane(image, fill, true, 0);
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

#ifdef EPD_DUAL_DEPTH
/* A 2bpp frame (or, with image NULL, a solid level from the two plane fills)
 * through the uploaded 4-gray waveform. The waveform and rails are broadcast
 * to both chips just before the planes; SWRESET in the next init clears them,
 * so the next mono paint is untouched. */
static void paint_gray(const uint8_t *image, uint8_t fill1, uint8_t fill2,
                       const char *what)
{
    bus_hold();
    cmd_data(SSD_BORDER,   BORDER_4G,  sizeof BORDER_4G);
    cmd_data(SSD_BOOST,    BOOST_4G,   sizeof BOOST_4G);
    cmd_data(SSD_LUT,      LUT_4G,     sizeof LUT_4G);
    cmd_data(SSD_END_OPT,  END_OPT_4G, sizeof END_OPT_4G);
#if SSD1683_GRAY_SET_RAILS
    cmd_data(SSD_GATE_V,   GATE_V_4G,  sizeof GATE_V_4G);
    cmd_data(SSD_SOURCE_V, SRC_V_4G,   sizeof SRC_V_4G);
#endif
#if SSD1683_GRAY_SET_VCOM
    cmd_data(SSD_VCOM,     VCOM_4G,    sizeof VCOM_4G);
#endif

    send_plane(image, fill1, false, 1);
    send_plane(image, fill1, true, 1);
    send_plane(image, fill2, false, 2);
    send_plane(image, fill2, true, 2);

    cmd_data(SSD_UPD_CTRL1, UPD_CTRL1_GRAY, sizeof UPD_CTRL1_GRAY);
    const uint8_t upd[] = {SSD_UPD_GRAY};
    int64_t t0 = esp_timer_get_time();
    cmd_data(SSD_UPD_CTRL2, upd, sizeof upd);
    cmd_data(SSD_TRIGGER, NULL, 0);
    wait_idle();
    ESP_LOGI(TAG, "%s done (4-gray, %lld ms)", what,
             (long long)((esp_timer_get_time() - t0) / 1000));
    bus_release();
    s_gray_dirty = true;
}
#endif /* EPD_DUAL_DEPTH */

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

#ifdef EPD_DUAL_DEPTH
static void ssd1683_display_gray(const uint8_t *image)
{
    paint_gray(image, 0, 0, "refresh");
}
#endif

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
#if defined(EPD_DUAL_DEPTH) && defined(EPD_SELFTEST)
/* Bring-up, gray: four full-height columns, black, dark gray, light gray and
 * white from the left, with a white wedge in the black column's top-left
 * corner. Even steps between the two greys are the tuning target; the wedge
 * gives orientation. Bit order and the seam are the mono selftest's job. */
static void ssd1683_show_color_bars(void)
{
    uint8_t *frame = malloc(EPD_GRAY_BUF_BYTES);
    if (!frame) return;
    const int stride = EPD_WIDTH / 4;
    const int WEDGE = 48;   /* px */

    for (int fy = 0; fy < EPD_HEIGHT; fy++) {
        uint8_t *row = frame + (size_t)fy * stride;
        for (int fx = 0; fx < EPD_WIDTH; fx++) {
            uint8_t g = (uint8_t)(fx * 4 / EPD_WIDTH);           /* 0..3 */
            if (fx < WEDGE && fy < WEDGE) g = 3;                  /* wedge */
            const int sh = (3 - (fx & 3)) * 2;
            row[fx >> 2] = (uint8_t)((row[fx >> 2] & ~(0x3 << sh)) | (g << sh));
        }
    }

    ssd1683_display_gray(frame);
    free(frame);
}

/* Transport check: alternating black and white pixels across the frame. */
static void ssd1683_show_palette_sweep(void)
{
    uint8_t *frame = malloc(EPD_GRAY_BUF_BYTES);
    if (!frame) return;
    memset(frame, 0x33, EPD_GRAY_BUF_BYTES);   /* black, white, black, white */
    ssd1683_display_gray(frame);
    free(frame);
}
#else
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
#endif /* EPD_DUAL_DEPTH && EPD_SELFTEST */

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
        .bpp       = 1,
        .grayscale = false,
        .width     = EPD_WIDTH,
        .height    = EPD_HEIGHT,
        .buf_bytes = EPD_BUF_BYTES,
    },
    .port_init          = ssd1683_port_init,
    .init               = ssd1683_init,
    .clear              = ssd1683_clear,
    .display            = ssd1683_display,
#ifdef EPD_DUAL_DEPTH
    .display_gray       = ssd1683_display_gray,
#endif
    .show_color_bars    = ssd1683_show_color_bars,
    .show_palette_sweep = ssd1683_show_palette_sweep,
    .sleep              = ssd1683_sleep,
};

#endif /* PANEL_DRIVER_SSD1683_DUAL */
