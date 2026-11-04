#!/bin/sh
# Host-side tests for the battery gauge mapping (src/battery.c battery_pct):
# the EE02 build's dedicated SenseCraft xiao_diy curve vs the generic
# two-segment curve. IDF headers are stubbed (test/battery_stubs); the ADC
# path itself is not exercised -- battery_pct() is pure.
set -eu

OUT="$(mktemp -d -t tesserae-battery-gauge.XXXXXX)"
cc -std=c11 -Wall -Wextra -Werror -DEXPECT_TABLE=1 \
   -DTESSERAE_BOARD_SEEED_EE02 \
   -I test/battery_stubs -I src -I include -I boards \
   test/test_battery_gauge.c test/battery_stubs.c src/battery.c \
   -o "$OUT/test_battery_table"
cc -std=c11 -Wall -Wextra -Werror -DEXPECT_TABLE=0 \
   -DTESSERAE_BOARD_XIAO_EPAPER_75 \
   -I test/battery_stubs -I src -I include -I boards \
   test/test_battery_gauge.c test/battery_stubs.c src/battery.c \
   -o "$OUT/test_battery_generic"
"$OUT/test_battery_table"
"$OUT/test_battery_generic"
