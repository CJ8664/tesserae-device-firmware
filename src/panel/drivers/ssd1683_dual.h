/*
 * Family F: two cascaded SSD1683 controllers behind one CS, mono 1bpp.
 *
 * Elecrow CrowPanel 5.79" (792x272). The SSD1683 is an SSD16xx-class part
 * like the SSD1677: 0x24 RAM plane, 0x22+0x20 to trigger, 0x44/0x45 windows,
 * BUSY high = busy. What it adds is the cascade: the slave answers its own
 * opcodes (the master's with bit 7 set: 0x91, 0xA4, 0xC4, ...) while plain
 * opcodes reach both chips.
 *
 * The frame is 1bpp packed, 8 px/byte MSB-first, bit 1 = white -- the same
 * wire format as every esp32_bw_bin panel, at 792x272 = 26928 bytes.
 *
 * Sequence from Elecrow's Arduino examples (RAM layout, entry modes) and
 * GxEPD2's GDEY0579T93 driver (update control, cascade clock).
 * UNVERIFIED ON HARDWARE.
 */
#pragma once

#include "panel/epd_panel.h"

extern const epd_driver_t ssd1683_dual_driver;
