/* Host-side tests for battery_pct(): the millivolt -> percent mapping.
 *
 * Built twice by tools/test_battery_gauge.sh against the real src/battery.c:
 *
 *   EXPECT_TABLE=1  -DTESSERAE_BOARD_SEEED_EE02
 *       The EE02 selects its own dedicated SenseCraft curve (the xiao_diy
 *       BATTERY_CURVE from Seeed's firmware, inverted into the tree's
 *       percent-indexed 101-point table). It previously used the generic
 *       curve below.
 *   EXPECT_TABLE=0  -DTESSERAE_BOARD_XIAO_EPAPER_75
 *       An ADC board with no table: the generic two-segment curve EE02
 *       used before, kept here as the reference mapping.
 *
 * Expected values are worked from the two mappings by hand. For the EE02 table,
 * the reference is SenseCraft's voltageToPercent() piecewise-linear lookup
 * on the xiao_diy curve, which gives 26.00 / 48.75 / 81.67 at
 * 3700 / 3900 / 4100 mV; soc_from_table()'s nearest-percent rounding lands
 * on 26 / 49 / 82. */
#include <assert.h>
#include <stdio.h>

#include "battery.h"

#ifndef EXPECT_TABLE
#define EXPECT_TABLE 0
#endif

static void check(int mv, int want)
{
    int got = battery_pct(mv);
    if (got != want)
        fprintf(stderr, "battery_pct(%d) = %d, want %d\n", mv, got, want);
    assert(got == want);
}

int main(void)
{
    check(0, 0);        /* unknown reading reports 0 either way */
    check(-1, 0);
#if EXPECT_TABLE
    /* EE02 xiao_diy curve: index is the percent, values are resting mV. */
    check(2900, 0);     /* below the table range (floor 2910 mV): clamped */
    check(2910, 0);     /* curve endpoint: 0% */
    check(3300, 6);     /* generic curve said 0 here */
    check(3547, 14);    /* just under the 15% always-on threshold ... */
    check(3548, 15);    /* ... first mV that reads AWAKE_BATTERY_MIN_PCT */
    check(3555, 15);    /* the curve's exact 15% point (table[15]) */
    check(3600, 18);    /* an exact curve point */
    check(3700, 26);    /* SenseCraft lookup: 26.00; generic said 30 */
    check(3900, 49);    /* SenseCraft lookup: 48.75; generic said 58 */
    check(4100, 82);    /* SenseCraft lookup: 81.67; generic said 86 */
    check(4150, 100);   /* curve endpoint: 100% */
    check(4200, 100);   /* above the table range: clamped */
    puts("battery gauge (EE02 xiao_diy curve): all tests passed");
#else
    /* Generic two-segment curve: 0% at 3300, 30% at 3700, 100% at 4200. */
    check(3000, 0);
    check(3300, 0);
    check(3600, 22);
    check(3700, 30);
    check(3900, 58);
    check(4100, 86);
    check(4200, 100);
    puts("battery gauge (generic curve): all tests passed");
#endif
    return 0;
}
