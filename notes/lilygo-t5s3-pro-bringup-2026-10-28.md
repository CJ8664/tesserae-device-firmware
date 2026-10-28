# LILYGO T5 E-Paper S3 Pro Lite bring-up, 2026-10-28

Unit: T5 E-Paper S3 **Pro Lite** (ESP32-S3-WROOM-1-N16R8, 16 MB flash, 8 MB
octal PSRAM, chip revision v0.2). The Lite uses the Pro's PCB, so its back cover
is silkscreened "T5-EPAPER ESP32-S3 LORA" even though no LoRa radio is fitted.
The Pro and Pro Lite share a schematic, so the header should serve both, but
only a Lite has been tested. Console on the S3's native USB-Serial-JTAG.
Board header `boards/lilygo_t5s3_pro.h`, ESP-IDF 5.5.2.

## Which LILYGO board is this?

Three different boards carry "T5 4.7 inch S3" names, and they are wired
differently. Getting this wrong cost a flash and an hour, so it is written down.

| Board | Panel power / control | Pin map source |
| --- | --- | --- |
| **T5 E-Paper S3 Pro** and **Pro Lite** (H752-01) | TPS65185 PMIC + PCA9535 expander, I2C SDA 39 / SCL 40 | LILYGO `T5S3-4.7-e-paper-PRO` (`examples/epd_60fps_probe`) |
| H752 (no TPS65185) | 74HCT4094 shift register | FastEPD `BB_PANEL_LILYGO_T5PRO` |
| T5-4.7 **S3** / **S3 Lite** (EPD47 family) | 74HCT4094 shift register | `LilyGo-EPD47`, branch `esp32s3` |

This header is for the first row only. The Pro Lite shares the Pro's schematic
and only omits LoRa and GPS (LILYGO's README version table). The panel path is
the epdiy v7's pin for pin (LILYGO's own examples drive it as
`BB_PANEL_EPDIY_V7`): data 5,6,7,15,16,17,18,8; WR 4; CS 41; LE 42; STV 45;
CKV 48; dummy D/C GPIO0; PCA9535 0x20; TPS65185 0x68; VCOM -1.6 V.

## Result

- `lilygo-t5s3-pro-selftest` powers the rails (no `PWRGOOD never rose` or TPS
  rail errors on the log), paints the scrambled sheet in about 1 s, and the
  ordered sheet 45 s later sorts black to white.
- The production build boots into the Wi-Fi portal (`Tesserae-Setup`), joins the
  home network, registers with a Tesserae 0.480.0 server, and paints
  server-composed 960x540 dashboards (weather,
  schedule, countdown): crisp text and icons, clean greys. The server logs show
  scheduled page pushes and the board's wake telemetry.
- Ordinary flashing over the native USB port needs no BOOT button, but if BOOT
  is held (or stuck) during a reset the chip boots into ROM download mode
  (`boot:0x1 DOWNLOAD(USB/UART0)`) instead of the app.

## Not done

- Battery (BQ25896 charger / BQ27220 gauge) life and charging, the GT911 touch
  panel, the PCF8563 RTC, the side button and the microSD slot. None are wired
  in the board header.
- Partial refresh (`EPD_PAR_PARTIAL`) is off.
- The grey matrices are the PaperS3's, tuned on M5's panel. This is the same
  ED047TC1 glass and the dashboard photo looks right, but the evenness of the
  16 bands has not been measured.
- Kind: the bring-up above ran with the firmware still reporting the PaperS3's
  kind `m5stack_papers3` (same 259200-byte wire format, landscape) against a stock
  server. The firmware now reports its own kind, `lilygo_t5s3_pro`, which needs
  the catalog entry in the matching Tesserae PR (`hardware/lilygo/t5s3_pro.json`);
  with that kind it has not been run end to end yet unless noted otherwise.
- OTA is off for this env and has not been exercised on the board. No
  `release.yml` row is added; that is the maintainer's call.
