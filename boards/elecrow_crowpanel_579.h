/*
 * Board: Elecrow CrowPanel ESP32 5.79" E-Paper HMI Display
 *   - MCU:   ESP32-S3-WROOM-1-N8R8 (8 MB flash, 8 MB octal PSRAM), CH340C
 *            USB-UART bridge on the USB-C port
 *   - Panel: 5.79" 792x272 black/white, two cascaded SSD1683 controllers
 *
 * Family F, ssd1683_dual. Each SSD1683 drives 396 of its 400 sources, master
 * on the left half and slave on the right; see ssd1683_dual.c for the RAM
 * layout and the 8-column seam it hides.
 *
 * Pins transcribed from Elecrow's schematic (Eagle_SCH&PCB in
 * Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792) and
 * cross-checked against their Arduino examples. Their wiki's pin table lists
 * the TF-card bus as the panel's; the schematic does not.
 *
 * UNVERIFIED ON HARDWARE.
 */
#pragma once

/* Panel pin map (one CS shared by both controllers, SPI2). */
#define EPD_PIN_SCLK   12
#define EPD_PIN_MOSI   11
#define EPD_PIN_CS     45
#define EPD_PIN_DC     46
#define EPD_PIN_RST    47
#define EPD_PIN_BUSY   48   /* active HIGH: 1 = busy */
/* GPIO7 drives a 2N7002 that switches the panel's ground (LCD_GND), so HIGH
 * powers the panel. A 100k pull holds it off through deep sleep. */
#define EPD_PIN_EN     7

#define EPD_SPI_HOST   SPI2_HOST
#define EPD_SPI_HZ     (10 * 1000 * 1000)

/* Frame geometry, 1bpp packed, 99 bytes per row = 26928 bytes. The driver
 * inserts the seam columns, so the frame is only the visible pixels. */
#define EPD_WIDTH      792
#define EPD_HEIGHT     272
#define EPD_BUF_BYTES  ((EPD_WIDTH * EPD_HEIGHT) / 8)

/* Dual depth: this firmware also paints 4-level grey frames, 2bpp packed, 4
 * px/byte MSB-first, 0b00 = black .. 0b11 = white, 53856 bytes. The frame's
 * length picks the path, and the server sends grey only to devices that
 * advertise it ("gamuts" on register and status) and are set to grey, so a
 * mono frame paints exactly as before. The grey path is checked on this glass
 * (bench 2026-10-20: selftest ramp and a live dashboard); the -selftest env
 * paints the grey ramp.
 */
#define EPD_DUAL_DEPTH      1
#define EPD_GRAY_BUF_BYTES  ((EPD_WIDTH * EPD_HEIGHT) / 4)   /* 2bpp = 53856 */

/* Elecrow's examples all draw at their Rotation 180, so that is taken as
 * upright until the selftest says otherwise: its white wedge should land in
 * the top-left corner. If it lands bottom-right, set this to 0. */
#define EPD_ROTATE_180 1

/* Mono palette (bit 1 = white), matching mono_spi / the E1001. */
#define EPD_COL_BLACK   0x0
#define EPD_COL_WHITE   0x1

#define TESSERAE_DEVICE_MODEL  "CrowPanel_579"

/* SERVER DEPENDENCY: hardware/elecrow/crowpanel_579.json, gamut mono, through
 * esp32_bw_bin at 792x272. */
#define TESSERAE_DEVICE_KIND   "elecrow_crowpanel_579"

#define TESSERAE_RELAY_MODEL   "esp32_bw_client"
#define TESSERAE_RELAY_GAMUT   "mono"

/* No battery sense: the BAT connector feeds the TP4054 charger and the 3.3 V
 * regulator, and the cell voltage reaches no ESP32 pin. */

/* Rotary switch and two keys, all active-low with 4.7k pull-ups and all in
 * GPIO0-21, so ext1 can wake on them. buttons.h knows three keys: the rotary
 * up/down move through the rotation and its press re-renders. MENU (GPIO2)
 * and EXIT (GPIO1) are left unbound for now. */
#define BOARD_BTN_REFRESH_PIN  5    /* rotary press (CONF) */
#define BOARD_BTN_LEFT_PIN     6    /* rotary up */
#define BOARD_BTN_RIGHT_PIN    4    /* rotary down */

/* Red power LED, active-high through 4.7k. */
#define BOARD_LED_PIN  41

/* microSD on its own SPI bus. GPIO42 switches the slot's ground like GPIO7
 * does the panel's: HIGH = powered. */
#define TESSERAE_SD_SLOT      1
#define SD_USE_DEDICATED_SPI  1
#define SD_SPI_HOST   SPI3_HOST
#define SD_PIN_SCLK   39
#define SD_PIN_MOSI   40
#define SD_PIN_MISO   13
#define SD_PIN_CS     10
#define SD_PIN_EN     42

/* The USB-C port is the CH340C; the S3's own USB pins (GPIO19/20) go to the
 * expansion header. Never consult that peripheral for "is a laptop attached". */
#define BOARD_USB_SERIAL_JTAG_ABSENT  1

/* MCU tier: ESP32-S3 + octal PSRAM. */
#define MCU_TIER_S3_OCTAL_PSRAM 1

#define PANEL_DRIVER_SSD1683_DUAL 1
