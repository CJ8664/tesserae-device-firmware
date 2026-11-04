#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "diag.h"

static diag_state_t fresh(uint32_t seed)
{
    diag_state_t s;
    memset(&s, 0xC3, sizeof s);   /* power-on garbage */
    diag_boot(&s, DIAG_RESET_NONE, seed, 0);
    return s;
}

static void test_power_on_starts_clean_with_seeded_ids(void)
{
    diag_state_t s = fresh(1000);
    assert(s.magic == DIAG_MAGIC && s.next_id == 1000);
    assert(!diag_get(&s, NULL));
}

static void test_abnormal_reset_latches_on_boot(void)
{
    diag_state_t s;
    memset(&s, 0, sizeof s);
    diag_boot(&s, DIAG_RESET_BROWNOUT, 500, 1790000000u);
    diag_report_t r;
    assert(diag_get(&s, &r));
    assert(r.id == 500 && r.reset == DIAG_RESET_BROWNOUT);
    assert(r.paint == DIAG_PAINT_NONE && r.at == 1790000000u);
}

static void test_paint_latch_reports_once_per_change(void)
{
    diag_state_t s = fresh(7);
    assert(diag_latch_paint(&s, DIAG_PAINT_READY_TIMEOUT, 0));
    /* A wait loop timing out on every command: no further changes. */
    assert(!diag_latch_paint(&s, DIAG_PAINT_READY_TIMEOUT, 5));
    /* The first failure of a report is the one it keeps. */
    assert(!diag_latch_paint(&s, DIAG_PAINT_REFRESH_TIMEOUT, 6));
    assert(!diag_latch_paint(&s, DIAG_PAINT_NONE, 6));
    diag_report_t r;
    assert(diag_get(&s, &r));
    assert(r.id == 7 && r.paint == DIAG_PAINT_READY_TIMEOUT && r.reset == DIAG_RESET_NONE);
    /* Clock unknown at the latch, known later: the later time fills in. */
    assert(r.at == 5);
}

static void test_clear_needs_the_delivered_id(void)
{
    diag_state_t s = fresh(41);
    diag_latch_paint(&s, DIAG_PAINT_INIT_FAILED, 100);
    diag_clear(&s, 40);
    assert(diag_get(&s, NULL));
    diag_clear(&s, 41);
    assert(!diag_get(&s, NULL));

    /* The next failure is a new report with a new id. */
    diag_latch_paint(&s, DIAG_PAINT_REFRESH_TIMEOUT, 200);
    diag_report_t r;
    assert(diag_get(&s, &r));
    assert(r.id == 42 && r.paint == DIAG_PAINT_REFRESH_TIMEOUT && r.at == 200);
}

static void test_report_survives_reboots_until_delivered(void)
{
    diag_state_t s = fresh(9);
    diag_latch_paint(&s, DIAG_PAINT_REFRESH_TIMEOUT, 300);

    /* Deep-sleep wake: kept as is. */
    diag_boot(&s, DIAG_RESET_NONE, 12345, 400);
    diag_report_t r;
    assert(diag_get(&s, &r) && r.id == 9 && r.at == 300);

    /* A brownout before it was delivered joins the same report. */
    diag_boot(&s, DIAG_RESET_BROWNOUT, 12345, 500);
    assert(diag_get(&s, &r));
    assert(r.id == 9 && r.paint == DIAG_PAINT_REFRESH_TIMEOUT);
    assert(r.reset == DIAG_RESET_BROWNOUT && r.at == 300);

    diag_clear(&s, 9);
    diag_boot(&s, DIAG_RESET_WATCHDOG, 12345, 600);
    assert(diag_get(&s, &r) && r.id == 10 && r.reset == DIAG_RESET_WATCHDOG);
    assert(r.paint == DIAG_PAINT_NONE);
}

static void test_corrupt_fields_restart_the_state(void)
{
    diag_state_t s = fresh(3);
    diag_latch_paint(&s, DIAG_PAINT_INIT_FAILED, 1);
    s.paint = 9;
    assert(!diag_get(&s, NULL));
    diag_boot(&s, DIAG_RESET_NONE, 77, 0);
    assert(!diag_get(&s, NULL) && s.next_id == 77);
}

/* ---------- sleep/wake accounting ---------- */

/* IDF enum values (esp_sleep.h, esp_system.h) used below. */
enum { WAKE_UNDEFINED = 0, WAKE_EXT1 = 3, WAKE_TIMER = 4 };
enum { RST_POWERON = 1, RST_SW = 3, RST_DEEPSLEEP = 8 };

static void test_sleep_first_boot_initialises(void)
{
    diag_sleep_state_t s;
    memset(&s, 0xC3, sizeof s);   /* power-on garbage */
    assert(!diag_sleep_get(&s, NULL));
    diag_sleep_boot(&s, WAKE_TIMER, RST_DEEPSLEEP);
    diag_sleep_report_t r;
    assert(diag_sleep_get(&s, &r));
    assert(r.boots == 1 && r.sleeps == 0);
    assert(r.wake_cause == WAKE_TIMER && r.reset_reason == RST_DEEPSLEEP);
    assert(r.sleep_s == 0 && r.awake_ms == 0 && r.cum_awake_ms == 0);
    assert(r.nap_ms == 0 && r.cum_nap_ms == 0);
}

static void test_sleep_cycles_accumulate(void)
{
    diag_sleep_state_t s;
    memset(&s, 0, sizeof s);
    diag_sleep_boot(&s, WAKE_UNDEFINED, RST_POWERON);
    diag_sleep_note_entry(&s, 3600, 5000, 3000);
    diag_sleep_report_t r;
    assert(diag_sleep_get(&s, &r));
    assert(r.sleep_s == 3600 && r.awake_ms == 5000 && r.cum_awake_ms == 5000);
    assert(r.nap_ms == 3000 && r.cum_nap_ms == 3000);
    assert(r.sleeps == 1);

    /* Next boot: the entry fields are replaced, the totals carry over. */
    diag_sleep_boot(&s, WAKE_EXT1, RST_DEEPSLEEP);
    diag_sleep_note_entry(&s, 1800, 7000, 0);
    assert(diag_sleep_get(&s, &r));
    assert(r.boots == 2 && r.sleeps == 2 && r.wake_cause == WAKE_EXT1);
    assert(r.nap_ms == 0 && r.cum_nap_ms == 3000);

    /* A cycle that restarts instead of sleeping: counted as a boot only. */
    diag_sleep_boot(&s, WAKE_UNDEFINED, RST_SW);
    assert(diag_sleep_get(&s, &r));
    assert(r.boots == 3 && r.sleeps == 2 && r.cum_awake_ms == 12000);
    assert(r.sleep_s == 1800 && r.awake_ms == 7000 && r.cum_awake_ms == 12000);
}

static void test_sleep_counters_saturate(void)
{
    diag_sleep_state_t s;
    memset(&s, 0, sizeof s);
    diag_sleep_boot(&s, WAKE_UNDEFINED, RST_POWERON);
    s.boots = UINT32_MAX;
    diag_sleep_boot(&s, WAKE_TIMER, RST_DEEPSLEEP);
    assert(s.boots == UINT32_MAX);
    s.sleeps = UINT32_MAX;
    s.cum_awake_ms = UINT64_MAX - 10;
    s.cum_nap_ms = UINT64_MAX - 10;
    diag_sleep_note_entry(&s, 60, 100, 100);
    assert(s.sleeps == UINT32_MAX && s.cum_awake_ms == UINT64_MAX);
    assert(s.cum_nap_ms == UINT64_MAX);
}

static void test_sleep_entry_before_boot_is_ignored(void)
{
    diag_sleep_state_t s;
    memset(&s, 0xC3, sizeof s);   /* never booted: invalid magic */
    diag_sleep_note_entry(&s, 3600, 5000, 0);
    assert(!diag_sleep_get(&s, NULL));
}

static void test_contract_names(void)
{
    assert(strcmp(diag_paint_name(DIAG_PAINT_INIT_FAILED), "init_failed") == 0);
    assert(strcmp(diag_paint_name(DIAG_PAINT_READY_TIMEOUT), "ready_timeout") == 0);
    assert(strcmp(diag_paint_name(DIAG_PAINT_REFRESH_TIMEOUT), "refresh_timeout") == 0);
    assert(diag_paint_name(DIAG_PAINT_NONE) == NULL);
    assert(strcmp(diag_reset_name(DIAG_RESET_BROWNOUT), "brownout") == 0);
    assert(strcmp(diag_reset_name(DIAG_RESET_PANIC), "panic") == 0);
    assert(strcmp(diag_reset_name(DIAG_RESET_WATCHDOG), "watchdog") == 0);
    assert(diag_reset_name(DIAG_RESET_NONE) == NULL);
}

int main(void)
{
    test_power_on_starts_clean_with_seeded_ids();
    test_abnormal_reset_latches_on_boot();
    test_paint_latch_reports_once_per_change();
    test_clear_needs_the_delivered_id();
    test_report_survives_reboots_until_delivered();
    test_corrupt_fields_restart_the_state();
    test_contract_names();
    test_sleep_first_boot_initialises();
    test_sleep_cycles_accumulate();
    test_sleep_counters_saturate();
    test_sleep_entry_before_boot_is_ignored();
    puts("diag: all tests passed");
    return 0;
}
