/*
 * diag_run.c: RTC-backed detected-failure latch. See diag_run.h.
 */
#include "diag_run.h"

#include <time.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "panel/busy_sleep.h"

static const char *TAG = "diag";

RTC_NOINIT_ATTR static diag_state_t s_diag;
RTC_NOINIT_ATTR static diag_sleep_state_t s_sleep;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_booted;
static uint32_t s_paint_failures;   /* this boot only */

/* Same plausibility window main.c uses for the wall clock. */
static uint32_t epoch_now(void)
{
    time_t t = time(NULL);
    return (t > 1700000000 && t < 2200000000LL) ? (uint32_t)t : 0;
}

static diag_reset_t reset_kind(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_BROWNOUT: return DIAG_RESET_BROWNOUT;
    case ESP_RST_PANIC:    return DIAG_RESET_PANIC;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return DIAG_RESET_WATCHDOG;
    default:               return DIAG_RESET_NONE;
    }
}

void diag_run_boot(void)
{
    if (s_booted) return;
    /* Random start, like the button event ids: the server dedups on id, and a
     * power-on must not replay ids it has already seen. Top bit clear so the
     * counter has room before it wraps. */
    uint32_t seed = esp_random() >> 1;
    uint32_t now = epoch_now();
    esp_reset_reason_t rr = esp_reset_reason();
    diag_reset_t rk = reset_kind(rr);
    uint32_t wake = (uint32_t)esp_sleep_get_wakeup_cause();
    diag_report_t rep;
    portENTER_CRITICAL(&s_mux);
    diag_boot(&s_diag, rk, seed, now);
    diag_sleep_boot(&s_sleep, wake, (uint32_t)rr);
    bool pending = diag_get(&s_diag, &rep);
    s_booted = true;
    portEXIT_CRITICAL(&s_mux);
    if (pending) {
        const char *pe = diag_paint_name(rep.paint);
        const char *rs = diag_reset_name(rep.reset);
        ESP_LOGW(TAG, "failure report %lu waiting: paint_error=%s reset=%s",
                 (unsigned long)rep.id, pe ? pe : "-", rs ? rs : "-");
    }
}

void diag_note_paint(diag_paint_t paint)
{
    if (!s_booted) diag_run_boot();
    s_paint_failures++;
    uint32_t now = epoch_now();
    portENTER_CRITICAL(&s_mux);
    bool changed = diag_latch_paint(&s_diag, paint, now);
    uint32_t id = s_diag.id;
    portEXIT_CRITICAL(&s_mux);
    if (changed)
        ESP_LOGW(TAG, "latched %s (report %lu)", diag_paint_name(paint),
                 (unsigned long)id);
}

uint32_t diag_run_paint_failures(void) { return s_paint_failures; }

bool diag_run_report(diag_report_t *out)
{
    if (!s_booted) diag_run_boot();
    portENTER_CRITICAL(&s_mux);
    bool pending = diag_get(&s_diag, out);
    portEXIT_CRITICAL(&s_mux);
    return pending;
}

void diag_run_clear(uint32_t id)
{
    portENTER_CRITICAL(&s_mux);
    diag_clear(&s_diag, id);
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "failure report %lu delivered", (unsigned long)id);
}

void diag_run_sleep_entry(uint32_t sleep_s)
{
    if (!s_booted) diag_run_boot();
    int64_t awake_ms64 = esp_timer_get_time() / 1000;   /* since app start */
    uint32_t awake_ms = awake_ms64 <= 0 ? 0
                      : awake_ms64 > UINT32_MAX ? UINT32_MAX : (uint32_t)awake_ms64;
    uint32_t nap_ms = epd_light_sleep_ms();
    portENTER_CRITICAL(&s_mux);
    diag_sleep_note_entry(&s_sleep, sleep_s, awake_ms, nap_ms);
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "sleep entry: programmed %lu s, awake %lu ms (%lu ms napping) this cycle",
             (unsigned long)sleep_s, (unsigned long)awake_ms, (unsigned long)nap_ms);
}

bool diag_run_sleep_report(diag_sleep_report_t *out)
{
    if (!s_booted) diag_run_boot();
    portENTER_CRITICAL(&s_mux);
    bool ok = diag_sleep_get(&s_sleep, out);
    portEXIT_CRITICAL(&s_mux);
    return ok;
}
