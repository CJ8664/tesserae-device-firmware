/*
 * diag.c: detected-failure latch. Pure C; see diag.h.
 */
#include "diag.h"

#include <string.h>

static bool valid(const diag_state_t *s)
{
    return s->magic == DIAG_MAGIC && s->magic_inv == (uint32_t)~DIAG_MAGIC &&
           s->pending <= 1 && s->paint < DIAG_PAINT_COUNT &&
           s->reset < DIAG_RESET_COUNT;
}

/* Open a latch if none is waiting, then fill in what it lacks. */
static bool latch(diag_state_t *s, diag_paint_t paint, diag_reset_t reset,
                  uint32_t now)
{
    bool changed = false;
    if (!s->pending) {
        s->pending = 1;
        s->id = s->next_id++;
        s->paint = DIAG_PAINT_NONE;
        s->reset = DIAG_RESET_NONE;
        s->at = now;
        changed = true;
    }
    if (paint != DIAG_PAINT_NONE && s->paint == DIAG_PAINT_NONE) {
        s->paint = (uint8_t)paint;
        changed = true;
    }
    if (reset != DIAG_RESET_NONE && s->reset == DIAG_RESET_NONE) {
        s->reset = (uint8_t)reset;
        changed = true;
    }
    if (s->at == 0) s->at = now;
    return changed;
}

void diag_boot(diag_state_t *s, diag_reset_t reset, uint32_t seed, uint32_t now)
{
    if (!valid(s)) {
        memset(s, 0, sizeof *s);
        s->magic = DIAG_MAGIC;
        s->magic_inv = (uint32_t)~DIAG_MAGIC;
        s->next_id = seed;
    }
    if (reset != DIAG_RESET_NONE && reset < DIAG_RESET_COUNT)
        latch(s, DIAG_PAINT_NONE, reset, now);
}

bool diag_latch_paint(diag_state_t *s, diag_paint_t paint, uint32_t now)
{
    if (paint == DIAG_PAINT_NONE || paint >= DIAG_PAINT_COUNT) return false;
    return latch(s, paint, DIAG_RESET_NONE, now);
}

bool diag_get(const diag_state_t *s, diag_report_t *out)
{
    if (!valid(s) || !s->pending) return false;
    if (out) {
        out->id = s->id;
        out->paint = (diag_paint_t)s->paint;
        out->reset = (diag_reset_t)s->reset;
        out->at = s->at;
    }
    return true;
}

void diag_clear(diag_state_t *s, uint32_t id)
{
    if (!s->pending || s->id != id) return;
    s->pending = 0;
    s->paint = DIAG_PAINT_NONE;
    s->reset = DIAG_RESET_NONE;
    s->at = 0;
}

const char *diag_paint_name(diag_paint_t p)
{
    switch (p) {
    case DIAG_PAINT_INIT_FAILED:     return "init_failed";
    case DIAG_PAINT_READY_TIMEOUT:   return "ready_timeout";
    case DIAG_PAINT_REFRESH_TIMEOUT: return "refresh_timeout";
    default:                         return NULL;
    }
}

const char *diag_reset_name(diag_reset_t r)
{
    switch (r) {
    case DIAG_RESET_BROWNOUT: return "brownout";
    case DIAG_RESET_PANIC:    return "panic";
    case DIAG_RESET_WATCHDOG: return "watchdog";
    default:                  return NULL;
    }
}

/* ---------- sleep/wake accounting (see diag.h) ---------- */

static bool sleep_valid(const diag_sleep_state_t *s)
{
    return s->magic == DIAG_SLEEP_MAGIC &&
           s->magic_inv == (uint32_t)~DIAG_SLEEP_MAGIC;
}

void diag_sleep_boot(diag_sleep_state_t *s, uint32_t wake_cause,
                     uint32_t reset_reason)
{
    if (!sleep_valid(s)) {
        memset(s, 0, sizeof *s);
        s->magic = DIAG_SLEEP_MAGIC;
        s->magic_inv = (uint32_t)~DIAG_SLEEP_MAGIC;
    }
    if (s->boots < UINT32_MAX) s->boots++;
    s->wake_cause = wake_cause;
    s->reset_reason = reset_reason;
}

static uint64_t add_sat(uint64_t a, uint32_t b)
{
    uint64_t r = a + b;
    return r < a ? UINT64_MAX : r;
}

void diag_sleep_note_entry(diag_sleep_state_t *s, uint32_t sleep_s,
                           uint32_t awake_ms, uint32_t nap_ms)
{
    if (!sleep_valid(s)) return;
    if (s->sleeps < UINT32_MAX) s->sleeps++;
    s->sleep_s = sleep_s;
    s->awake_ms = awake_ms;
    s->nap_ms = nap_ms;
    s->cum_awake_ms = add_sat(s->cum_awake_ms, awake_ms);
    s->cum_nap_ms = add_sat(s->cum_nap_ms, nap_ms);
}

bool diag_sleep_get(const diag_sleep_state_t *s, diag_sleep_report_t *out)
{
    if (!sleep_valid(s) || s->boots == 0) return false;
    if (out) {
        out->boots = s->boots;
        out->sleeps = s->sleeps;
        out->wake_cause = s->wake_cause;
        out->reset_reason = s->reset_reason;
        out->sleep_s = s->sleep_s;
        out->awake_ms = s->awake_ms;
        out->nap_ms = s->nap_ms;
        out->cum_awake_ms = s->cum_awake_ms;
        out->cum_nap_ms = s->cum_nap_ms;
    }
    return true;
}
