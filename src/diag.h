/*
 * diag.h: detected-failure latch, reported on the next /status as "diag"
 * (device log upload plan, Protocol v1). Pure logic over a state struct the
 * firmware keeps in RTC_NOINIT memory (diag_run.h); the host tests drive it
 * directly (test/test_diag.c).
 *
 * One latch at a time. The first failure after a delivered report opens it
 * with a new id; later failures before it is delivered only fill in a field it
 * does not have yet, so a panel failing every wake while offline reports one
 * failure, not one per wake. A status POST that carried the report and came
 * back 2xx clears it (diag_clear with the id that was sent).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DIAG_PAINT_NONE = 0,
    DIAG_PAINT_INIT_FAILED,      /* controller never answered after recovery */
    DIAG_PAINT_READY_TIMEOUT,    /* controller busy / HRDY line stuck */
    DIAG_PAINT_REFRESH_TIMEOUT,  /* refresh never reported done */
    DIAG_PAINT_COUNT
} diag_paint_t;

typedef enum {
    DIAG_RESET_NONE = 0,         /* power-on, deep-sleep wake, software reset */
    DIAG_RESET_BROWNOUT,
    DIAG_RESET_PANIC,
    DIAG_RESET_WATCHDOG,
    DIAG_RESET_COUNT
} diag_reset_t;

#define DIAG_MAGIC 0x44494147u   /* 'DIAG' */

typedef struct {
    uint32_t magic;
    uint32_t next_id;    /* id the next opened latch takes */
    uint32_t id;         /* id of the open latch */
    uint32_t at;         /* epoch of the latch, 0 if the clock was unknown */
    uint8_t  pending;    /* 1 while a report waits for delivery */
    uint8_t  paint;      /* diag_paint_t */
    uint8_t  reset;      /* diag_reset_t */
    uint8_t  pad;
    uint32_t magic_inv;
} diag_state_t;

typedef struct {
    uint32_t     id;
    diag_paint_t paint;
    diag_reset_t reset;
    uint32_t     at;
} diag_report_t;

/* Start of a boot: validate the retained state (a power-on or anything that
 * does not hold together restarts it, ids from `seed`), then latch `reset` if
 * this boot was abnormal. */
void diag_boot(diag_state_t *s, diag_reset_t reset, uint32_t seed, uint32_t now);

/* Record a paint failure. Returns true when this changed the report (so the
 * caller logs once, not once per timed-out poll). */
bool diag_latch_paint(diag_state_t *s, diag_paint_t paint, uint32_t now);

/* The report waiting for delivery, if any. */
bool diag_get(const diag_state_t *s, diag_report_t *out);

/* A status carrying report `id` was accepted. A latch reopened since (a
 * different id) stays. */
void diag_clear(diag_state_t *s, uint32_t id);

/* Contract names: "init_failed" / "ready_timeout" / "refresh_timeout", and
 * "brownout" / "panic" / "watchdog". NULL for NONE. */
const char *diag_paint_name(diag_paint_t p);
const char *diag_reset_name(diag_reset_t r);

/* ---------- sleep/wake accounting ----------
 *
 * A retained record of how the device actually cycles: how many boots, why
 * the last one woke, how long the last sleep was programmed for, and how
 * much of each cycle was spent awake. The failure latch above can only say
 * that something went wrong; this record is what separates "awake far
 * longer than the nominal cycle" from "asleep but still draining" when a
 * battery goes down faster than the code-visible energy costs explain.
 * Kept beside the latch (same RTC_NOINIT lifetime, validated the same way,
 * so it also survives a RESET press) and reported on /status as a separate
 * "sleep_stats" object; unlike the latch it is never cleared by delivery.
 * Boards that power off instead of sleeping (BOARD_RTC_POWER_OFF) lose it
 * every cycle. Pure logic over a state struct, like the latch; diag_run.*
 * owns the RTC copy and the IDF calls. */

#define DIAG_SLEEP_MAGIC 0x41434354u   /* 'ACCT' */

typedef struct {
    uint32_t magic;
    uint32_t boots;         /* boots since the record was (re)initialised */
    uint32_t sleeps;        /* deep-sleep entries. Mid-cycle (when /status
                               is sent) boots - sleeps - 1 = cycles that
                               ended otherwise (restart, crash) and so are
                               missing from the awake figures */
    uint32_t wake_cause;    /* esp_sleep_wakeup_cause_t of the latest boot */
    uint32_t reset_reason;  /* esp_reset_reason_t of the latest boot */
    uint32_t sleep_s;       /* timer seconds programmed at the last sleep
                               entry; 0 = no timer (indefinite sleep) */
    uint32_t awake_ms;      /* uptime of the last completed cycle */
    uint32_t nap_ms;        /* of which in light-sleep naps (panel BUSY) */
    uint64_t cum_awake_ms;  /* cumulative awake_ms across all cycles */
    uint64_t cum_nap_ms;    /* cumulative nap_ms across all cycles */
    uint32_t magic_inv;
} diag_sleep_state_t;

typedef struct {
    uint32_t boots;
    uint32_t sleeps;
    uint32_t wake_cause;
    uint32_t reset_reason;
    uint32_t sleep_s;
    uint32_t awake_ms;
    uint32_t nap_ms;
    uint64_t cum_awake_ms;
    uint64_t cum_nap_ms;
} diag_sleep_report_t;

/* Start of a boot: validate the retained record (anything that does not
 * hold together restarts it), count the boot, and record why
 * it woke. wake_cause / reset_reason are the raw IDF enum values, passed
 * in as integers so this file stays free of IDF headers. */
void diag_sleep_boot(diag_sleep_state_t *s, uint32_t wake_cause,
                     uint32_t reset_reason);

/* Sleep entry: count it, record the programmed timer and close out the
 * cycle's awake and nap time. sleeps and the cum_* totals saturate rather
 * than wrap. A record that was never booted (invalid magic) is ignored. */
void diag_sleep_note_entry(diag_sleep_state_t *s, uint32_t sleep_s,
                           uint32_t awake_ms, uint32_t nap_ms);

/* The latest snapshot. False when no valid record exists yet. */
bool diag_sleep_get(const diag_sleep_state_t *s, diag_sleep_report_t *out);
