/*
 * Board: LILYGO T5 4.7" E-Paper S3 Pro / Pro Lite (H752-01)
 *   - MCU:   ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM), native USB
 *   - Panel: 4.7" ED047TC1, 960x540, 16-level greyscale, RAW PARALLEL glass --
 *            the same glass as the M5Stack PaperS3
 *   - Power: TPS65185 PMIC (panel rails, VCOM) with its enables behind a
 *            PCA9535 I/O expander, both on I2C SDA 39 / SCL 40
 *
 * The Pro and the Pro Lite share one schematic (the Lite just omits LoRa and
 * GPS), and the panel path is pin for pin the epdiy v7's: LilyGo's own
 * examples/epd_60fps_probe (t5s3_epd_pins.h, epd_video.cpp) and its FastEPD
 * examples both drive it as BB_PANEL_EPDIY_V7. So this is the parallel_epd_gray
 * driver's v7 path (EPD_PAR_POWER_TPS65185, EPD_PAR_ROW_START_V7) with this
 * panel's geometry and the PaperS3's grey matrices.
 *
 * NOT the older "T5-4.7 S3" / "T5 S3 Lite" (EPD47 family, 74HCT4094) and NOT
 * the H752 (no TPS65185): those wire the panel differently.
 *
 * UNVERIFIED ON HARDWARE. Flash lilygo-t5s3-pro-selftest first and judge the
 * 16 grey bands: a blank panel with "PWRGOOD never rose" on the serial log
 * points at the TPS65185 power path.
 */
#pragma once

/* ------------------------------------------------------------------ */
/* Parallel bus (LilyGo t5s3_epd / epdiy v7 map)                       */
/* ------------------------------------------------------------------ */
#define EPD_PAR_DATA_PINS   {5, 6, 7, 15, 16, 17, 18, 8}
#define EPD_PAR_BUS_WIDTH   8
#define EPD_PAR_PCLK_HZ     (20 * 1000 * 1000)
#define EPD_PIN_CL          4    /* pixel clock; the i80 bus's WR line */
#define EPD_PIN_SPH         41   /* start pulse horizontal; the i80 bus's CS */
#define EPD_PIN_LE          42   /* latch shifted row into the driver outputs */
#define EPD_PIN_SPV         45   /* start pulse vertical (gate driver token) */
#define EPD_PIN_CKV         48   /* gate driver clock; one pulse = one row */
#define EPD_PIN_DC_DUMMY    0    /* the i80 driver wants a D/C pin; LilyGo gives it GPIO0 */
#define EPD_PAR_LINE_PADDING 16

#define EPD_PAR_ROW_START_V7    1

/* ------------------------------------------------------------------ */
/* Panel power: TPS65185 behind a PCA9535 expander                     */
/* ------------------------------------------------------------------ */
#define EPD_PAR_POWER_TPS65185  1
#define EPD_PAR_I2C_PORT        0
#define EPD_PAR_I2C_SDA         39
#define EPD_PAR_I2C_SCL         40

/* VCOM in millivolts, as a positive number: LilyGo's examples run -1.6 V. */
#ifndef EPD_VCOM_MV
#define EPD_VCOM_MV         1600
#endif

/* ------------------------------------------------------------------ */
/* Panel geometry: 960 across, 540 down, 4bpp = 259200 bytes           */
/* ------------------------------------------------------------------ */
#define EPD_WIDTH           960
#define EPD_HEIGHT          540
#define EPD_BUF_BYTES       ((EPD_WIDTH * EPD_HEIGHT) / 2)   /* 259200 */

#define EPD_COL_BLACK       0x0
#define EPD_COL_WHITE       0xF

#define EPD_PAR_PANEL_NAME  "LilyGo T5 S3 Pro 4.7\" (960x540, 4bpp)"

#define EPD_PAR_GRAY_MATRIX { \
    /*  0 */ 1, 1, 1, 1, 1, 1, 1, 1, \
    /*  1 */ 2, 2, 1, 1, 2, 1, 1, 1, \
    /*  2 */ 2, 2, 1, 1, 1, 1, 2, 1, \
    /*  3 */ 2, 2, 2, 1, 1, 1, 2, 1, \
    /*  4 */ 2, 2, 1, 1, 2, 2, 1, 1, \
    /*  5 */ 1, 1, 1, 1, 1, 1, 2, 2, \
    /*  6 */ 2, 2, 1, 1, 2, 1, 1, 2, \
    /*  7 */ 2, 2, 2, 2, 1, 1, 2, 1, \
    /*  8 */ 2, 1, 2, 1, 1, 1, 2, 2, \
    /*  9 */ 2, 1, 2, 1, 1, 1, 2, 2, \
    /* 10 */ 2, 2, 2, 2, 2, 1, 2, 1, \
    /* 11 */ 1, 1, 1, 1, 2, 1, 2, 2, \
    /* 12 */ 2, 2, 1, 2, 1, 1, 2, 2, \
    /* 13 */ 2, 2, 1, 1, 2, 1, 2, 2, \
    /* 14 */ 2, 1, 1, 2, 2, 1, 2, 2, \
    /* 15 */ 2, 2, 2, 2, 2, 2, 2, 2 }

/* Candidate matrix under evaluation (selftest paints it on the bottom half
 * of the sheet; normal display ignores it until promoted).
 *
 * Derived from the epdiy project's hand-built ED047TC1 waveform -- the same
 * glass -- mode MODE_EPDIY_WHITE_TO_GL16 (uniform white to 16-level target,
 * exactly this driver's starts-from-white contract), 20-30 C band:
 * https://github.com/vroland/epdiy src/waveforms/epdiy_ED047TC1.h (LGPL-3.0).
 * Extraction: for target level L, the 2-bit code at [phase][to=L][from=15]
 * across the 15 phases.
 *
 * The shape is a pulse-width ramp: level L darkens for 15-L passes from
 * white, then rests. Unlike a mixed darken/lighten pattern this is monotone
 * by construction -- more darken passes is strictly darker -- so the failure
 * mode to look for on glass is uneven SPACING (crushed darks or lights),
 * never a level inversion. 15 passes vs the shipped 8 costs ~25 ms per
 * refresh if promoted. */
#define EPD_PAR_GRAY_MATRIX_B { \
    /*  0 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
    /*  1 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, \
    /*  2 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, \
    /*  3 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, \
    /*  4 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, \
    /*  5 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, \
    /*  6 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, \
    /*  7 */ 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, \
    /*  8 */ 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, \
    /*  9 */ 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
    /* 10 */ 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
    /* 11 */ 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
    /* 12 */ 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
    /* 13 */ 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
    /* 14 */ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
    /* 15 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }

/* Board model -> default device id "LilyGoT5S3Pro_<mac-suffix>". */
#define TESSERAE_DEVICE_MODEL  "LilyGoT5S3Pro"

/* Tesserae hardware-catalog kind (hardware/lilygo/t5s3_pro.json in the Tesserae
 * server repo). The wire format is the PaperS3's (esp32_gray_bin, 960x540 4bpp,
 * 259200 B), used landscape. A kind is also the OTA lineage, so it is fixed from
 * first release. OTA is off for this env until it has been exercised on the
 * board. */
#define TESSERAE_DEVICE_KIND   "lilygo_t5s3_pro"

#define TESSERAE_RELAY_MODEL   "esp32_client"
#define TESSERAE_RELAY_GAMUT   "gray_16"

/* MCU tier: ESP32-S3 + 8 MB octal PSRAM. */
#define MCU_TIER_S3_OCTAL_PSRAM 1

#define PANEL_DRIVER_PARALLEL_EPD_GRAY 1
